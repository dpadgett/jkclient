/*
===========================================================================
Copyright (C) 1999 - 2005, Id Software, Inc.
Copyright (C) 2000 - 2013, Raven Software, Inc.
Copyright (C) 2001 - 2013, Activision, Inc.
Copyright (C) 2005 - 2015, ioquake3 contributors
Copyright (C) 2013 - 2015, OpenJK contributors

This file is part of the OpenJK source code.

OpenJK is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License version 2 as
published by the Free Software Foundation.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, see <http://www.gnu.org/licenses/>.
===========================================================================
*/

// cg_consolecmds.c -- text commands typed in at the local console, or
// executed by a key binding

#include <curl/curl.h>
#include "cg_local.h"
#include "game/bg_saga.h"
#include "ui/ui_shared.h"
//#include "unzip.h"
#include "cg_threads.h"
extern menuDef_t *menuScoreboard;
extern displayContextDef_t cgDC;

/*
=================
CG_TargetCommand_f

=================
*/
void CG_TargetCommand_f( void ) {
	int		targetNum;
	char	test[4];

	targetNum = CG_CrosshairPlayer();
	if ( targetNum == -1 ) {
		return;
	}

	trap->Cmd_Argv( 1, test, 4 );
	trap->SendClientCommand( va( "gc %i %i", targetNum, atoi( test ) ) );
}

/*
=================
CG_SizeUp_f

Keybinding command
=================
*/
static void CG_SizeUp_f (void) {
	trap->Cvar_Set( "cg_viewsize", va( "%i", Q_min( cg_viewsize.integer + 10, 100 ) ) );
}

/*
=================
CG_SizeDown_f

Keybinding command
=================
*/
static void CG_SizeDown_f (void) {
	trap->Cvar_Set( "cg_viewsize", va( "%i", Q_max( cg_viewsize.integer - 10, 30 ) ) );
}

/*
=============
CG_Viewpos_f

Debugging command to print the current position
=============
*/
static void CG_Viewpos_f (void) {
	trap->Print ("%s (%i %i %i) : %i\n", cgs.mapname, (int)cg.refdef.vieworg[0],
		(int)cg.refdef.vieworg[1], (int)cg.refdef.vieworg[2],
		(int)cg.refdef.viewangles[YAW]);
}

static int Q_isdigit( char *c ) {
	int i;
	if( c[0] == '-' ) c++;
	for( i=0; c[i]!=0; i++ )
		if (!(c[i] >= '0' && c[i] <= '9' || c[i] == '.'))
			return ( 0 );
	return ( 1 );
}

static void CG_SeenEnemyLoc( vec3_t ret ) {
	int nearest = -1;
	double nearestDist = 0;
	int i;
	vec3_t me;
	VectorCopy( cg.snap->ps.origin, me );
	me[2] += cg.snap->ps.viewheight;
	for( i = 0; i < cg.snap->numEntities; i++ )
		if( cgs.gametype >= GT_TEAM && cgs.clientinfo[cg.snap->entities[i].clientNum].team != cg.snap->ps.persistant[PERS_TEAM] )
		{
			vec3_t out, out2;
			double dist;
			VectorSubtract( cg.snap->entities[i].origin, me, out );
			dist = VectorLength( out );
			vectoangles( out, out2 );
			AnglesSubtract( out2, cg.snap->ps.viewangles, out );
			if( VectorLength( out ) < 70 )
			{
				if( nearest == -1 || dist < nearestDist )
				{
					nearestDist = dist;
					nearest = i;
				}
			}
		}
	if( nearest == -1 )
		VectorCopy( cg.snap->ps.origin, ret );
	else VectorCopy( cg.snap->entities[nearest].origin, ret );
}

extern vec3_t deathloc;

static const char *CG_StringForLocation( vec3_t origin ) {
	vec3_t tmp;
	float nearest;
	int i, nIdx = 0;

	if ( cgs.numLocations == 0 ) return "";

	// search through locations for nearest ent
	VectorSubtract( origin, cgs.locations[0].pos, tmp );
	nearest = VectorLength( tmp );
	for ( i = 0; i < cgs.numLocations; i++ ) {
		VectorSubtract( origin, cgs.locations[i].pos, tmp );
		if ( nearest > VectorLength( tmp ) ) {
			nIdx = i;
			nearest = VectorLength( tmp );
		}
	}

	return cgs.locations[nIdx].str;
}

/***********************************

$viewposX - player view's X world position (same value as posX)
$viewposY - player view's Y world position (same value as posY)
$viewposZ - player view's Z world position (posZ + viewheight)
$viewposXYZ - player view's X Y Z world position

$posX - player's X world position
$posY - player's Y world position
$posZ - player's Z world position
$posXYZ - player's X Y Z world position

$angX - player's X viewangle
$angY - player's Y viewangle
$angZ - player's Z viewangle
$angXYZ - player's X Y Z viewangle

$velX - player's X velocity
$velY - player's Y velocity
$velZ - player's Z velocity
$velXYZ - player's X Y Z velocity

$speedXY - player's horizontal speed
$speedZ  - player's vertical speed

$mapname    - current map name
$date       - current date in YYYYMMDD format
$time       - current time in HHMMSSS format
$playername - player's name (colors removed)
$cgtime     - clientside timer

************************************/
static void CG_VarMath_f(void) {
	int i;
	char varName[MAX_STRING_CHARS];
	char tmpBuf[MAX_STRING_CHARS],op;
	double curVal = 0;
	char curValS[MAX_STRING_CHARS];
	qboolean isString = qfalse;
	if(trap->Cmd_Argc() < 2) {
		Com_Printf("\n^1varMath: ^2Performs math functions on cvars and vstrs.\n");
		Com_Printf("^7Usage:   varMath targetVariableName operator: +-*/%= value (+-*/%= value)\n");
		Com_Printf("^2Example: ^7varMath timescale * .2\n");
		Com_Printf("^2Example: ^7varMath user_num = 1 + 3 * 5\n");
		Com_Printf("^2Example: ^7varMath user_demoname = $mapname + _ + $date + _ + 001\n\n");
		Com_Printf("^3Note:    for immediate variable update, prefix the targetVariableName with \"user_\"\n");
		return;
	}

	if(trap->Cmd_Argc() < 3) {
		Com_Printf( "^7*** Error - missing operator\n" );
		return;
	}

	if(trap->Cmd_Argc() < 4) {
		Com_Printf( "^7*** Error - missing value\n" );
		return;
	}
	trap->Cmd_Argv( 1, varName, sizeof( varName ) );
	trap->Cvar_VariableStringBuffer( varName, curValS, sizeof( curValS ) );
	curVal = atof( curValS );
	for ( i = 2; i < trap->Cmd_Argc(); i += 2 ) {
		trap->Cmd_Argv( i, tmpBuf, sizeof( tmpBuf ) );
		op = tmpBuf[0];
		trap->Cmd_Argv( i + 1, tmpBuf, sizeof( tmpBuf ) );
		if( tmpBuf[0] == '$' ) {
			if( !strcmp( tmpBuf+1, "viewposX" ) || !strcmp( tmpBuf+1, "posX" ) ) {
				Q_strncpyz( tmpBuf, va( "%f", cg_entities[cg.snap->ps.clientNum].lerpOrigin[0] ), sizeof( tmpBuf ) );
			} else if( !strcmp( tmpBuf+1, "viewposY" ) || !strcmp( tmpBuf+1, "posY" ) ) {
				Q_strncpyz( tmpBuf, va( "%f", cg_entities[cg.snap->ps.clientNum].lerpOrigin[1] ), sizeof( tmpBuf ) );
			} else if( !strcmp( tmpBuf+1, "viewposZ" ) ) {
				Q_strncpyz( tmpBuf, va( "%f", cg_entities[cg.snap->ps.clientNum].lerpOrigin[2]+cg.snap->ps.viewheight ), sizeof( tmpBuf ) );
			} else if( !strcmp( tmpBuf+1, "viewposXYZ" ) ) {
				Q_strncpyz( tmpBuf, va( "%f %f %f",
					cg_entities[cg.snap->ps.clientNum].lerpOrigin[0],
					cg_entities[cg.snap->ps.clientNum].lerpOrigin[1],
					cg_entities[cg.snap->ps.clientNum].lerpOrigin[2]+cg.snap->ps.viewheight ), sizeof( tmpBuf ) );
			} else if( !strcmp( tmpBuf+1, "posZ" ) ) {
				Q_strncpyz( tmpBuf, va( "%f", cg_entities[cg.snap->ps.clientNum].lerpOrigin[2] ), sizeof( tmpBuf ) );
			} else if( !strcmp( tmpBuf+1, "posXYZ" ) ) {
				Q_strncpyz( tmpBuf, va( "%f %f %f",
					cg_entities[cg.snap->ps.clientNum].lerpOrigin[0],
					cg_entities[cg.snap->ps.clientNum].lerpOrigin[1],
					cg_entities[cg.snap->ps.clientNum].lerpOrigin[2] ), sizeof( tmpBuf ) );
			} else if( !strcmp( tmpBuf+1, "angX" ) ) {
				Q_strncpyz( tmpBuf, va( "%f", cg_entities[cg.snap->ps.clientNum].lerpAngles[0] ), sizeof( tmpBuf ) );
			} else if( !strcmp( tmpBuf+1, "angY" ) ) {
				Q_strncpyz( tmpBuf, va( "%f", cg_entities[cg.snap->ps.clientNum].lerpAngles[1] ), sizeof( tmpBuf ) );
			} else if( !strcmp( tmpBuf+1, "angZ" ) ) {
				Q_strncpyz( tmpBuf, va( "%f", cg_entities[cg.snap->ps.clientNum].lerpAngles[2] ), sizeof( tmpBuf ) );
			} else if( !strcmp( tmpBuf+1, "angXYZ" ) ) {
				Q_strncpyz( tmpBuf, va( "%f %f %f",
					cg_entities[cg.snap->ps.clientNum].lerpAngles[0],
					cg_entities[cg.snap->ps.clientNum].lerpAngles[1],
					cg_entities[cg.snap->ps.clientNum].lerpAngles[2] ), sizeof( tmpBuf ) );
			} else if( !strcmp( tmpBuf+1, "velX" ) ) {
				Q_strncpyz( tmpBuf, va( "%f", cg_entities[cg.snap->ps.clientNum].playerState->velocity[0] ), sizeof( tmpBuf ) );
			} else if( !strcmp( tmpBuf+1, "velY" ) ) {
				Q_strncpyz( tmpBuf, va( "%f", cg_entities[cg.snap->ps.clientNum].playerState->velocity[1] ), sizeof( tmpBuf ) );
			} else if( !strcmp( tmpBuf+1, "velZ" ) || !strcmp( tmpBuf+1, "speedZ" ) ) {
				Q_strncpyz( tmpBuf, va( "%f", cg_entities[cg.snap->ps.clientNum].playerState->velocity[2] ), sizeof( tmpBuf ) );
			} else if( !strcmp( tmpBuf+1, "velXYZ" ) ) {
				Q_strncpyz( tmpBuf, va( "%f %f %f",
					cg_entities[cg.snap->ps.clientNum].playerState->velocity[0],
					cg_entities[cg.snap->ps.clientNum].playerState->velocity[1],
					cg_entities[cg.snap->ps.clientNum].playerState->velocity[2] ), sizeof( tmpBuf ) );
			} else if( !strcmp( tmpBuf+1, "speedXY" ) ) {
				Q_strncpyz( tmpBuf, va( "%f", sqrt(cg_entities[cg.snap->ps.clientNum].playerState->velocity[0] * cg_entities[cg.snap->ps.clientNum].playerState->velocity[0]+cg_entities[cg.snap->ps.clientNum].playerState->velocity[1] * cg_entities[cg.snap->ps.clientNum].playerState->velocity[1]) ), sizeof( tmpBuf ) );
			} else if( !strcmp( tmpBuf+1, "mapname" ) ) {
				Q_strncpyz( tmpBuf, COM_SkipPath(cgs.mapname), sizeof( tmpBuf ) );
				COM_StripExtension( tmpBuf, tmpBuf, sizeof( tmpBuf ) );
			} else if( !strcmp( tmpBuf+1, "date" ) ) {
				qtime_t ct;
				trap->RealTime(&ct);
				Q_strncpyz( tmpBuf, va( "%04d%02d%02d", ct.tm_year + 1900, ct.tm_mon+1,ct.tm_mday ), sizeof( tmpBuf ) );
				isString = qtrue;
			} else if( !strcmp( tmpBuf+1, "time" ) ) {
				qtime_t ct;
				trap->RealTime(&ct);
				Q_strncpyz( tmpBuf, va( "%02d%02d%02d", ct.tm_hour, ct.tm_min, ct.tm_sec ), sizeof( tmpBuf ) );
				isString = qtrue;
			} else if( !strcmp( tmpBuf+1, "playername" ) ) {
				Q_strncpyz( tmpBuf, cgs.clientinfo[ cg.snap->ps.clientNum ].name, sizeof( tmpBuf ) );
			} else if( !strcmp( tmpBuf+1, "cgtime" ) ) {
				Q_strncpyz( tmpBuf, va( "%d" , cg.time ), sizeof( tmpBuf ) );
			} else if( !strcmp( tmpBuf+1, "loc" ) ) {
				Q_strncpyz( tmpBuf, CG_StringForLocation( cg_entities[cg.snap->ps.clientNum].lerpOrigin ), sizeof( tmpBuf ) );
			} else if( !strcmp( tmpBuf+1, "deathloc" ) ) {
				Q_strncpyz( tmpBuf, CG_StringForLocation( deathloc ), sizeof( tmpBuf ) );
			} else if( !strcmp( tmpBuf+1, "seenenemyloc" ) ) {
				vec3_t loc;
				CG_SeenEnemyLoc( loc );
				Q_strncpyz( tmpBuf, CG_StringForLocation( loc ), sizeof( tmpBuf ) );
			} else if( !strcmp( tmpBuf+1, "force" ) ) {
				Q_strncpyz( tmpBuf, va( "%d", cg.snap->ps.fd.forcePower ), sizeof( tmpBuf ) );
			} else if( !strcmp( tmpBuf+1, "space" ) ) {
				Q_strncpyz( tmpBuf, " ", sizeof( tmpBuf ) );
			} else if( !strcmp( tmpBuf+1, "health" ) ) {
				Q_strncpyz( tmpBuf, va( "%d", cg.snap->ps.stats[STAT_HEALTH] ), sizeof( tmpBuf ) );
			} else if( !strcmp( tmpBuf+1, "shields" ) ) {
				Q_strncpyz( tmpBuf, va( "%d", cg.snap->ps.stats[STAT_ARMOR] ), sizeof( tmpBuf ) );
			} else {
				trap->Cvar_VariableStringBuffer( tmpBuf+1, tmpBuf, sizeof( tmpBuf ) );
			}
		}
		switch( op ) {
			case '+':
			{
				if( Q_isdigit( tmpBuf ) && !isString ) {
					curVal += atof( tmpBuf );
				} else {
					Q_strcat( curValS, sizeof(curValS), tmpBuf );
					isString = qtrue;
				}
				break;
			}
			case '-':
			{
				curVal -= atof( tmpBuf );
				break;
			}
			case '*':
			{
				curVal *= atof( tmpBuf );
				break;
			}
			case '/':
			{
				curVal /= atof( tmpBuf );
				break;
			}
			case '=':
			{
				if( Q_isdigit( tmpBuf ) && !isString ) {
					curVal = atof( tmpBuf );
				} else {
					Q_strncpyz( curValS, tmpBuf, sizeof(curValS));
					isString = qtrue;
				}
				break;
			}
			case '%':
			{
				int tmpcurVal;
				tmpcurVal = (int)curVal;
				tmpcurVal %= atoi( tmpBuf );
				curVal = (double)tmpcurVal;
				break;
			}
			default:
			{
				Com_Printf("*** Parse error: invalid operator (%c)\n",op);
				return;
			}
		}
	}

	if( !Q_strncmp( varName, "user_", strlen( "user_" ) ) ) {
		if( isString ) {
			trap->Cvar_Set( varName, curValS );
			Com_Printf( "^7%s ^4-->^7 %s\n", varName, curValS );
		} else if( ((float)((int) curVal)) == curVal ) {
			trap->Cvar_Set( varName, va( "%i", (int)curVal ) );
			Com_Printf( "^7%s ^4-->^7 %d\n", varName, (int)curVal );
		} else {
			trap->Cvar_Set( varName, va( "%f", curVal ) );
			Com_Printf( "^7%s ^4-->^7 %f\n", varName, curVal );
		}
	} else {
		if( isString ) {
			trap->SendConsoleCommand( va( "set %s %s", varName, curValS ) );
			Com_Printf( "^7%s ^4-->^7 %s\n", varName, curValS );
		} else if( ((float)((int) curVal)) == curVal ) {
			trap->SendConsoleCommand( va( "set %s %d", varName, (int)curVal ) );
			Com_Printf( "^7%s ^4-->^7 %d\n", varName, (int)curVal );
		} else {
			trap->SendConsoleCommand( va( "set %s %f", varName, curVal ) );
			Com_Printf( "^7%s ^4-->^7 %f\n", varName, curVal );
		}
	}
}

#ifdef WEB_DOWNLOAD
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
static int Q_vsnprintf2( char *dest, int size, const char *fmt, va_list argptr ) {
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
	ret = Q_vsnprintf2 (dest, size, fmt, argptr);
	va_end (argptr);
	if (ret == -1) {
		Com_Printf ("Com_sprintf: overflow of %i bytes buffer\n", size);
	}
}

void CG_ReadableSize ( char *buf, int bufsize, int value ) {
	if (value > 1024*1024*1024 ) { // gigs
		Com_sprintf2( buf, bufsize, "%d", value / (1024*1024*1024) );
		Com_sprintf2( buf+strlen(buf), bufsize-strlen(buf), ".%02d GB", 
			(value % (1024*1024*1024))*100 / (1024*1024*1024) );
	} else if (value > 1024*1024 ) { // megs
		Com_sprintf2( buf, bufsize, "%d", value / (1024*1024) );
		Com_sprintf2( buf+strlen(buf), bufsize-strlen(buf), ".%02d MB", 
			(value % (1024*1024))*100 / (1024*1024) );
	} else if (value > 1024 ) { // kilos
		Com_sprintf2( buf, bufsize, "%d KB", value / 1024 );
	} else { // bytes
		Com_sprintf2( buf, bufsize, "%d bytes", value );
	}
}

/*
===========
CG_FilenameCompare

Ignore case and seprator char distinctions
===========
*/
qboolean CG_FilenameCompare( const char *s1, const char *s2 ) {
	int		c1, c2;
	
	do {
		c1 = *s1++;
		c2 = *s2++;

		if (c1 >= 'a' && c1 <= 'z') {
			c1 -= ('a' - 'A');
		}
		if (c2 >= 'a' && c2 <= 'z') {
			c2 -= ('a' - 'A');
		}

		if ( c1 == '\\' || c1 == ':' ) {
			c1 = '/';
		}
		if ( c2 == '\\' || c2 == ':' ) {
			c2 = '/';
		}
		
		if (c1 != c2) {
			return qtrue;		// strings not equal
		}
	} while (c1);
	
	return qfalse;		// strings are equal
}

const int NUM_RAVEN_PAKS = 4;

/*
================
CG_ravenPak
================
*/
qboolean CG_ravenPak( char *pak, char *base ) {
	int i;

	for (i = 0; i < NUM_RAVEN_PAKS; i++) {
		if ( !CG_FilenameCompare(pak, va("%s/assets%d", base, i)) ) {
			break;
		}
	}
	if (i < NUM_RAVEN_PAKS) {
		return qtrue;
	}
	return qfalse;
}

/*
================
CG_ensimodPak
================
*/
qboolean CG_ensimodPak( char *pak ) {
	/*int i;

	if ( !CG_FilenameCompare(pak, "ensimod/ensimod_dlls") ) {
		return qtrue;
	}

	for (i = 0; i < NUM_RAVEN_PAKS+2; i++) {	// allow for 6 (0-5)
		if ( !CG_FilenameCompare(pak, va("ensimod/ensimod_assets%d",i)) ) {
			break;
		}
	}

	if (i < NUM_RAVEN_PAKS+2) {
		return qtrue;
	}*/
	return qfalse;
}

struct FtpFile {
	char *filename;
	char realfilename[MAX_STRING_CHARS];
	char basepath[MAX_STRING_CHARS];
	FILE *stream;
};

void FS_ReplaceSeparators( char *path ) {
	char	*s;

	for ( s = path ; *s ; s++ ) {
		if ( *s == '/' || *s == PATH_SEP ) {
			*s = PATH_SEP;
		}
	}
}

char testpath[MAX_STRING_CHARS];

qboolean FS_FileExists( const char *file )
{
	FILE *f;
	static char testpatht[MAX_STRING_CHARS];
	char testpath[MAX_STRING_CHARS];
	
	if( !strlen( testpatht ) )
		trap->Cvar_VariableStringBuffer("fs_homepath", testpatht, sizeof(testpatht));
	
	Q_strncpyz( testpath, testpatht, sizeof( testpath ) );

	Q_strcat(testpath, sizeof(testpath), "\\base\\");
	Q_strcat(testpath, sizeof(testpath), file);

	FS_ReplaceSeparators( testpath );

	f = fopen( testpath, "rb" );
	if (f) {
		fclose( f );
		return qtrue;
	}
	return qfalse;
}

//DWORD WINAPI download_thread(char *file);
void *download_thread(void *file_args);
void CG_InitItems(void);
void CG_RegisterCvars(void);

//extern void CG_LoadHudMenu_dl(void);
//extern void CG_LoadNewHud(void);
void initGfx(void) {
    static qboolean loaded=qfalse;
    if(loaded==qfalse) {
        trap->RegisterSharedMemory(cg.sharedBuffer.raw);
//		BG_ClearScriptTranslationPool();
//		BG_LoadLanguageScript( "translation.cfg" );
    	CG_Init_CGents();
    	CG_Init_CG();
    	CG_InitItems();
    	cgs.media.charsetShader		= trap->R_RegisterShaderNoMip( "gfx/2d/charsgrid_med" );
    	cgs.media.whiteShader		= trap->R_RegisterShader( "white" );
		cgs.media.cursor			= trap->R_RegisterShaderNoMip( "cursor" );
    	cgs.media.loadBarLED		= trap->R_RegisterShaderNoMip( "gfx/hud/load_tick" );
    	cgs.media.loadBarLEDCap		= trap->R_RegisterShaderNoMip( "gfx/hud/load_tick_cap" );
    	cgs.media.loadBarLEDSurround= trap->R_RegisterShaderNoMip( "gfx/hud/mp_levelload" );
    	CG_RegisterCvars();
//		CG_InitMemory();
    	cgDC.Assets.qhSmallFont  = trap->R_RegisterFont("ocr_a");
    	cgDC.Assets.qhMediumFont = trap->R_RegisterFont("ergoec");
    	cgDC.Assets.qhBigFont = cgDC.Assets.qhMediumFont;
//    	CG_LoadHudMenu_dl();
//		CG_LoadNewHud();
    	trap->GetGameState( &cgs.gameState );
    	CG_ParseServerinfo();
//		BG_LoadLanguageScript( va("translations/%s.dat", COM_SkipPath(cgs.rawmapname)) );
    	loaded = qtrue;
    }
}

#ifdef WIN32
void _mkdir(char *);
#endif
/*
============
FS_CreatePath

Creates any directories needed to store the given filename
============
*/
static qboolean FS_CreatePath (char *OSPath) {
	char	*ofs;
	
	// make absolutely sure that it can't back up the path
	// FIXME: is c: allowed???
	if ( strstr( OSPath, ".." ) || strstr( OSPath, "::" ) ) {
		//Com_Printf( "WARNING: refusing to create relative path \"%s\"\n", OSPath );
		return qtrue;
	}

	for (ofs = OSPath+1 ; *ofs ; ofs++) {
		if (*ofs == PATH_SEP) {	
			// create the directory
			*ofs = 0;
#ifdef WIN32
			_mkdir (OSPath);
#else
			mkdir (OSPath, 0777);
#endif
			*ofs = PATH_SEP;
		}
	}
	return qfalse;
}

static void FS_CopyFile( char *fromOSPath, char *toOSPath ) {
	FILE	*f;
	int		len;
	byte	*buf;

	//Com_Printf( "copy %s to %s\n", fromOSPath, toOSPath );

	/*if (strstr(fromOSPath, "journal.dat") || strstr(fromOSPath, "journaldata.dat")) {
		Com_Printf( "Ignoring journal files\n");
		return;
	}*/

	f = fopen( fromOSPath, "rb" );
	if ( !f ) {
		return;
	}
	fseek (f, 0, SEEK_END);
	len = ftell (f);
	fseek (f, 0, SEEK_SET);

	// we are using direct malloc instead of Z_Malloc here, so it
	// probably won't work on a mac... Its only for developers anyway...
	buf = malloc( len );
	if (fread( buf, 1, len, f ) != len)
		;//Com_Error( ERR_FATAL, "Short read in FS_Copyfiles()\n" );
	fclose( f );

	if( FS_CreatePath( toOSPath ) ) {
		return;
	}

	f = fopen( toOSPath, "wb" );
	if ( !f ) {
		return;
	}
	if (fwrite( buf, 1, len, f ) != len)
		;//Com_Error( ERR_FATAL, "Short write in FS_Copyfiles()\n" );
	fclose( f );
	free( buf );
}

extern unsigned Com_BlockChecksum (void *buffer, int length);
int startt,t,curfile=0;
char base[MAX_STRING_CHARS]="",refPaks[MAX_STRING_CHARS]="";
char referer[MAX_STRING_CHARS]="";
qboolean checksumProb=qfalse;

extern qboolean BG_FileExists(const char *fileName);
int runthrostringpls(char *files) {
    int i=0,needsrestart=0;
    static int rly=0;
    static char lastfile[MAX_STRING_CHARS]="";
    qboolean rdy=qfalse;
	//qboolean success;
	char mapname[MAX_STRING_CHARS], *fullname;
	const char *info;
	curfile=0;
    
    cgs.is_downloading = 1;
	if(!strlen(testpath)){trap->Cvar_VariableStringBuffer("fs_homepath", testpath, sizeof(testpath));Q_strcat(testpath, sizeof(testpath), "\\base\\");}
	if(!strlen(base)) {
		trap->Cvar_VariableStringBuffer( "g_dlURL", base, MAX_STRING_CHARS );
		if (!strlen(base)) {
			trap->Cvar_VariableStringBuffer( "cl_wwwBaseUrl", base, MAX_STRING_CHARS );
		}
	}
	/*if (base[strlen(base)-1] == '\\' || base[strlen(base)-1] == '/') {
		base[strlen(base)-1] = 0;
	}*/
	if(!strlen(refPaks))trap->Cvar_VariableStringBuffer("sv_referencedPaks", refPaks, sizeof(refPaks));
	if(!strlen(referer)) {
		char sv_address[MAX_STRING_CHARS];
		trap->Cvar_VariableStringBuffer("cl_currentServerAddress", sv_address, sizeof(sv_address));
		Com_sprintf2( referer, sizeof(referer), "jka://%s", sv_address);
	}

	trap->GetGameState( &cgs.gameState );
//    CG_ParseServerinfo();
	info = CG_ConfigString( CS_SERVERINFO );
	Q_strncpyz( mapname, Info_ValueForKey( info, "mapname" ), sizeof( mapname ) );
	fullname = va( "maps/%s.bsp", mapname );
	
	if( !BG_FileExists( fullname ) )
	{
		char pk3name[MAX_QPATH], *c, *filestart;
		initGfx();
		//Q_strncpyz( cg.infoScreenText, cgs.mapname, sizeof( cg.infoScreenText ) );
		Com_Printf( "DOWNLOAD: retrieving map %s\n", mapname );
		trap->Cvar_Set( "cl_triedDownload", "1" );
		//dl teh goodz
		//Q_strncpyz( cg.infoScreenText, files, sizeof( cg.infoScreenText ) );
		Q_strncpyz( cgs.dlname, va("%s.bsp",mapname), sizeof( cgs.dlname ) );
		trap->Cvar_Set("cl_downloadName",va("%s.bsp",mapname));
		startt = trap->Milliseconds();
//		Q_strncpyz( lastfile, files, sizeof( lastfile ) );
		//success = Web_Get( base, NULL, files, qtrue, 30, NULL );
		// map name should be the one referenced pak that's not assets.  in theory, lol :s
		for (c = files, filestart = files; ; *c++) {
			if (*c == ' ' || *c == '\0') {
				Q_strncpyz( pk3name, filestart, Q_min( sizeof( pk3name ), c - filestart + 1 ) );
				Com_Printf( "Checking pak \"%s\"\n", pk3name );
				if ( Q_strncmp( pk3name, "assets", strlen( "assets" ) ) && Q_strncmp( pk3name, "pug_map_pool", strlen( "pug_map_pool" ) ) ) {
					// pk3 is not an assets pk3.  so probably the map?
					break;
				} else {
					// not it
					pk3name[0] = '\0';
				}
			}
			if ( *c == ' ' || *c == '/' ) {
				filestart = c + 1;
			}
			if ( *c == '\0' ) {
				break;
			}
		}
		if ( pk3name[0] != '\0' ) {
			create_thread(download_thread, (void *)va("%s.pk3",pk3name));
			//CreateThread( NULL, 0, (LPTHREAD_START_ROUTINE)download_thread, (LPVOID)va("%s.pk3",files), 0, NULL );
			needsrestart=-1;
		} else {
			Com_Printf( "None of the referenced paks match: %s\n", files );
		}
	}

    /*for(i=0;i<strlen(files);i++) {
        if(files[i]==','||i+1==strlen(files)) {
            if(files[i]==',')files[i]=0;
            curfile++;
            if(!strlen(lastfile))rdy=qtrue;
            if(rdy&&(strlen(files)>1 && files[strlen(files)-1]!='_') &&
               !FS_FileExists( va("%s.pk3",files) ) &&
               !CG_ravenPak( files, "base" ) &&
               !CG_ensimodPak( files ) ) {
				initGfx();
				//Q_strncpyz( cg.infoScreenText, cgs.mapname, sizeof( cg.infoScreenText ) );
				Com_Printf( "DOWNLOAD: retrieving %s.pk3\n", files );
				//dl teh goodz
				//Q_strncpyz( cg.infoScreenText, files, sizeof( cg.infoScreenText ) );
				Q_strncpyz( cgs.dlname, va("%s.pk3",files), sizeof( cgs.dlname ) );
				trap_Cvar_Set("cl_downloadName",va("%s.pk3",files));
				startt = trap_Milliseconds();
				Q_strncpyz( lastfile, files, sizeof( lastfile ) );
				//success = Web_Get( base, NULL, files, qtrue, 30, NULL );
				create_thread(download_thread, (void *)va("%s.pk3",files));
				//CreateThread( NULL, 0, (LPTHREAD_START_ROUTINE)download_thread, (LPVOID)va("%s.pk3",files), 0, NULL );
				needsrestart=-1;
				break;
            } else if(rdy&&(strlen(files)>1 && files[strlen(files)-1]!='_') &&
               FS_FileExists( va("%s.pk3",files) ) &&
               !CG_ravenPak( files, "base" ) &&
               !CG_ensimodPak( files ) ) {
                unzFile			uf;
                unz_global_info gi;
                unz_file_info	file_info;
                int				fs_numHeaderLongs;
                int				*fs_headerLongs;
                char			filename_inzip[MAX_QPATH];
                int				err;
				unsigned long	checkSum=0;
				char			serverCheckSum[MAX_STRING_CHARS];
				char			*serverChecksum;
				int				checkSum2=0;
				int				j=0,ii=0;
				serverChecksum = serverCheckSum;
                fs_numHeaderLongs = 0;
				
				uf = unzOpen(va("%s.pk3",files));
				err = unzGetGlobalInfo (uf,&gi);
				
				if (err == UNZ_OK)
				{
					fs_headerLongs = calloc( gi.number_entry * sizeof(int), 1 );
					unzGoToFirstFile(uf);
					for (ii = 0; ii < gi.number_entry; ii++)
					{
						err = unzGetCurrentFileInfo(uf, &file_info, filename_inzip, sizeof(filename_inzip), NULL, 0, NULL, 0);
						if (err != UNZ_OK) {
							break;
						}
						if (file_info.uncompressed_size > 0) {
							fs_headerLongs[fs_numHeaderLongs++] = LittleLong(file_info.crc);
						}
						unzGoToNextFile(uf);
					}
					checkSum = Com_BlockChecksum( fs_headerLongs, 4 * fs_numHeaderLongs );
					free(fs_headerLongs);
				}
                trap_Cvar_VariableStringBuffer("sv_referencedPaks", serverCheckSum, sizeof(serverCheckSum));
                while(j!=curfile-1) {
					if(*serverChecksum==' '&&serverChecksum[1]!=' ')j++;
					serverChecksum++;
				}
				j=0;
				while(serverChecksum[j++]) {
					if(serverChecksum[j]==' ')serverChecksum[j]=0;
				}
				checkSum2 = atoi( serverChecksum );
				if(checkSum!=checkSum2) {
					//first to close off the file - dis b UBER HAX :oops
					int myFile,j;
					trap_FS_FOpenFile( va( "%s.pk3",files), &myFile, FS_READ );
					for(j=0;j<=myFile;j++)
						trap_FS_FCloseFile( j );  //LMFAO OWNED!  we just closed every open file in the game, lets hope everything we need is loaded in mem :oops
					FS_CopyFile( va( "%s.pk3", files ), va( "%s.%08x.pk3", files, checkSum ) );
					remove( va( "%s.pk3", files ) );
	                initGfx();
	                //Q_strncpyz( cg.infoScreenText, cgs.mapname, sizeof( cg.infoScreenText ) );
					Com_Printf( "DOWNLOAD: retrieving %s.pk3 (bad checksum)\n", files );
	                //dl teh goodz
	                //Q_strncpyz( cg.infoScreenText, files, sizeof( cg.infoScreenText ) );
	                Q_strncpyz( cgs.dlname, va("%s.pk3",files), sizeof( cgs.dlname ) );
	                trap_Cvar_Set("cl_downloadName",va("%s.pk3",files));
	                startt = trap_Milliseconds();
	                Q_strncpyz( lastfile, files, sizeof( lastfile ) );
	                //success = Web_Get( base, NULL, files, qtrue, 30, NULL );
					create_thread(download_thread, (void *)va("%s.pk3",files));
					//CreateThread( NULL, 0, (LPTHREAD_START_ROUTINE)download_thread, (LPVOID)va("%s.pk3",files), 0, NULL );
	                needsrestart=-1;
	                break;
				}
			}
			if(strcmp( files, lastfile )==0)rdy=qtrue;
            files+=i+1;
            i=0;
        }
    }
    if(!needsrestart && rly) {
        //only way to force a fs_restart is to change the fs_game cvar then
        //force the game to call fs_conditionalrestart.  dont worry fs_game
        //will be correctly reset upon reload and the only thing that will
        //change is that the pk3s will be refreshed. :)
        if( checksumProb )
			trap_Cvar_Set( "cl_checksumProblem", "1" );
			trap_Cvar_Set("fs_game","base");
			trap_SendConsoleCommand("donedl\n");
    }
	cgs.is_downloading = needsrestart?qtrue:qfalse;
	if(needsrestart)rly=-1;*/
    return needsrestart;
}

int ListFiles ( void ) {
	char files[MAX_STRING_CHARS];
    char filess[MAX_STRING_CHARS]="";
    //int i;

	CG_InitThreads(); // we gotta init threads before we call any pthread stuff on linux/mac

    trap->Cvar_VariableStringBuffer("cl_checksumProblem", files, sizeof(files));
    if(atoi(files)==1) {
		Com_Printf("DOWNLOAD: cl_checksumProblem is 1, bailing out\n");
		trap->Cvar_Set( "cl_checksumProblem", "0" );
		return 0;
	}
	trap->Cvar_VariableStringBuffer("cl_triedDownload", files, sizeof(files));
    if(atoi(files)==1) {
		Com_Printf("DOWNLOAD: cl_triedDownload is 1, bailing out\n");
		trap->Cvar_Set( "cl_triedDownload", "0" );
		return 0;
	}
    /*trap->Cvar_VariableStringBuffer("g_dlURL", files, sizeof(files));
    if(atoi(files)==0) {
		Com_Printf("DOWNLOAD: g_dlURL is 0, bailing out\n");
		//return 0;
	}*/
	trap->Cvar_VariableStringBuffer( "cl_wwwDownload", files, sizeof( files ) );
	if(atoi(files)==0) {
		Com_Printf("DOWNLOAD: cl_wwwDownload is 0, bailing out\n");
		return 0;
	}
	trap->Cvar_VariableStringBuffer( "sv_referencedPakNames", files, sizeof( files ) );

    /*for(i=0;i<strlen(files);i++) {
        if(files[i]==' ') {
			Com_sprintf2(filess, sizeof(filess),"%s,",filess);
            //sprintf(filess,"%s,",filess);
            while(files[i+1]==' ')i++;
		} else Com_sprintf2(filess, sizeof(filess),"%s%c",filess,files[i]);
        //} else sprintf(filess,"%s%c",filess,files[i]);
    }*/
	
	return runthrostringpls(files);
}

void *progress_callback (void* fp, double dltotal, double dlnow, double ultotal, double ulnow)	
{
   	/*char dlSizeBuf[64], totalSizeBuf[64];
   	CG_ReadableSize( dlSizeBuf, sizeof(dlSizeBuf), dlnow);
	CG_ReadableSize( totalSizeBuf, sizeof(totalSizeBuf), dltotal);
	Com_Printf( "DOWNLOAD: %s of %s copied (%.0lf%%)\n", dlSizeBuf, totalSizeBuf, dltotal>0?dlnow/dltotal * 100.0:0.);*/
	cgs.dltotal = dltotal;
	cgs.dlnow = dlnow;
	t = 0;
	//Sleep(1000);
	//use 2 slow down 4 testin ui :nervou
	return 0;
}

int my_fwrite(void *buffer, size_t size, size_t nmemb, void *stream)
{
	struct FtpFile *out=(struct FtpFile *)stream;
	if(out && !out->stream) {
		/* open file for writing */
		out->stream=fopen(out->filename, "wb");
		if(!out->stream)
			return -1; /* failure, can't open file to write */
	}
	return fwrite(buffer, size, nmemb, out->stream);
}

static char	* QDECL _va( char *format, ... ) {
	va_list		argptr;
#define MAX_VA_STRING 32000
	static char		temp_buffer[MAX_VA_STRING];
	static char		string[MAX_VA_STRING];	// in case va is called by nested functions
	static int		index = 0;
	char	*buf;
	int len;

	va_start (argptr, format);
	vsprintf (temp_buffer, format,argptr);
	va_end (argptr);

	if ((len = strlen(temp_buffer)) >= MAX_VA_STRING) {
		Com_Error( ERR_DROP, "Attempted to overrun string in call to va()\n" );
	}

	if (len + index >= MAX_VA_STRING-1) {
		index = 0;
	}

	buf = &string[index];
	memcpy( buf, temp_buffer, len+1 );

	index += len + 1;

	return buf;
}

//static char	* QDECL _va( char *format, ... )_attribute((format(printf,1,2)));

/*void *plz2download(void *args) {
	int success;
	cg_downloadInfo_t *dlInfo = (cg_downloadInfo_t*)args;

	success = Web_Get(dlInfo->remoteurl, referer, dlInfo->localfile, 0, 0, NULL);
	return 0;
}*/

size_t headers( void *ptr, size_t size, size_t nmemb, void *userdata)
{
	char header[MAX_STRING_CHARS];
	struct FtpFile *ftpfile = (struct FtpFile *)userdata;
	strcpy( header, (char *)ptr );
	if ( !Q_strncmp( header, "HTTP/1.0 404", strlen( "HTTP/1.0 404" ) ) ) {
		// fails download
		cgs.is_downloading = 3;
		return size*nmemb - 1;
	}
	header[41] = header[size*nmemb-1] = header[size*nmemb-2] = 0;
	if( !Q_stricmp( header, "Content-Disposition: attachment; filename" ) )
	{
		char *head = header;
		// name of file is rite here
		if( *(head+42) == '\\' ) head++;
		strcpy( ftpfile->realfilename, head+42 );
		if( ftpfile->basepath[strlen(ftpfile->basepath)-1] == '\\' ) ftpfile->basepath[strlen(ftpfile->basepath)-1] = 0;
		ftpfile->filename = _va( "%s\\%s", ftpfile->basepath, head+42 );
	}
	else
	{
		header[39] = 0;
		if( !Q_stricmp( header, "Warning: 199 File Not Found In Database" ) )
		{
			// map is not in db
			cgs.is_downloading = 3;
			return size*nmemb - 1;
		}
	}
	return size*nmemb;
}

int curl_res = 0;

//DWORD WINAPI download_thread(char *fileptr)
void *download_thread(void *file_args)
{
	CURL *curl;
	CURLcode res;
	struct FtpFile ftpfile;
	char file[MAX_STRING_CHARS];
	char mapname[MAX_STRING_CHARS];
	char *fileptr = (char *)file_args;
	strcpy( file, fileptr );
	
	//Sleep(10 * 1000);

    ftpfile.filename = _va( "%s%s", testpath, file ); /* name to store the file as if succesful */
	strcpy( ftpfile.basepath, testpath );
    ftpfile.stream = NULL;
	ftpfile.realfilename[0] = 0;

	strcpy( mapname, file );

	curl = curl_easy_init();
	if(curl) 
	{
		curl_easy_setopt(curl, CURLOPT_URL, _va( "%sbase/%s", base, file ));

		curl_easy_setopt(curl, CURLOPT_REFERER, referer);

		curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, my_fwrite);
		curl_easy_setopt(curl, CURLOPT_WRITEDATA, &ftpfile);
		//outfile = fopen( va( "%s\\%s", testpath, file), "wb" );
		//curl_easy_setopt(curl, CURLOPT_WRITEDATA, &outfile);
		curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0);
		curl_easy_setopt(curl, CURLOPT_PROGRESSFUNCTION ,&progress_callback );

		curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, headers);
		curl_easy_setopt(curl, CURLOPT_WRITEHEADER, &ftpfile);

		curl_easy_setopt(curl, CURLOPT_VERBOSE, 1);
		curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1);
		
		res = curl_easy_perform(curl);

		curl_easy_cleanup(curl);

//		Sleep( 5 * 1000 );

		if(CURLE_OK != res) 
		{
			/* we failed */
			//Com_Printf("curl told us %d\n", res);
			//plz 2 not do an error :nervou
			cgs.is_downloading = 3;
			curl_res = res;
		}
	}

	if(ftpfile.stream)
		fclose(ftpfile.stream); /* close the local file */
    
	curl_global_cleanup();

	//copyfile_success = FileCopy(map_filename.string, va("ensimod/%s", map_filename.string));
	/*if(FS_FileExists(_va("%s.tmp",file)))
        FS_CopyFile( _va("%s.tmp",file), file );

	if( FS_FileExists(file) ) {
		unzFile			uf;
        unz_global_info gi;
        unz_file_info	file_info;
        int				fs_numHeaderLongs;
        int				*fs_headerLongs;
        char			filename_inzip[MAX_QPATH];
        int				err;
		unsigned long	checkSum=0;
		char			serverCheckSum[MAX_STRING_CHARS];
		char			*serverChecksum;
		int				checkSum2=0;
		int				j=0,ii=0;
		remove(_va("%s.tmp",file));
		serverChecksum = serverCheckSum;
        fs_numHeaderLongs = 0;
		
		uf = unzOpen( file );
		err = unzGetGlobalInfo (uf,&gi);
		
		if (err == UNZ_OK)
		{
			fs_headerLongs = calloc( gi.number_entry * sizeof(int), 1 );
			unzGoToFirstFile(uf);
			for (ii = 0; ii < gi.number_entry; ii++)
			{
				err = unzGetCurrentFileInfo(uf, &file_info, filename_inzip, sizeof(filename_inzip), NULL, 0, NULL, 0);
				if (err != UNZ_OK) {
					break;
				}
				if (file_info.uncompressed_size > 0) {
					fs_headerLongs[fs_numHeaderLongs++] = LittleLong(file_info.crc);
				}
				unzGoToNextFile(uf);
			}
			checkSum = Com_BlockChecksum( fs_headerLongs, 4 * fs_numHeaderLongs );
			free(fs_headerLongs);
		}
        Q_strncpyz( serverCheckSum, refPaks, sizeof( serverCheckSum ) );
        while(j!=curfile-1) {
			if(*serverChecksum==' '&&serverChecksum[1]!=' ')j++;
			serverChecksum++;
		}
		j=0;
		while(serverChecksum[j++]) {
			if(serverChecksum[j]==' ')serverChecksum[j]=0;
		}
		checkSum2 = atoi( serverChecksum );
		if(checkSum!=checkSum2) {
			//download failed - got file with incorrect checksum
			//remove( _va( "%s.pk3", files ) );
			checksumProb = qtrue;
		}
		//Com_Printf ("file is in place, restart jk3\n");
	}
	else
	{
		cgs.is_downloading = 3;
		return 0;
		//Com_Printf ("copy failed, check url?\n");
		//:oops
	}*/
	
	if( cgs.is_downloading == 1 ) cgs.is_downloading = 2;

	return 0;
}

#endif

/*void curl_getmap(void)
{
	CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)&download_thread, NULL, 0, NULL);
}*/

/*
=================
CG_ScoresDown_f
=================
*/
static void CG_ScoresDown_f( void ) {

	CG_BuildSpectatorString();
	if ( cg.scoresRequestTime + 2000 < cg.time ) {
		// the scores are more than two seconds out of data,
		// so request new ones
		cg.scoresRequestTime = cg.time;
		trap->SendClientCommand( "score" );

		// leave the current scores up if they were already
		// displayed, but if this is the first hit, clear them out
		if ( !cg.showScores ) {
			cg.showScores = qtrue;
			cg.numScores = 0;
		}
	} else {
		// show the cached contents even if they just pressed if it
		// is within two seconds
		cg.showScores = qtrue;
	}
}

/*
=================
CG_ScoresUp_f

=================
*/
static void CG_ScoresUp_f( void ) {
	if ( cg.showScores ) {
		cg.showScores = qfalse;
		cg.scoreFadeTime = cg.time;
	}
}

void CG_ClientList_f( void )
{
	clientInfo_t *ci;
	int i;
	int count = 0;

	for( i = 0; i < MAX_CLIENTS; i++ )
	{
		ci = &cgs.clientinfo[ i ];
		if( !ci->infoValid )
			continue;

		switch( ci->team )
		{
		case TEAM_FREE:
			Com_Printf( "%2d " S_COLOR_YELLOW "F   " S_COLOR_WHITE "%s" S_COLOR_WHITE "%s\n", i, ci->name, (ci->botSkill != -1) ? " (bot)" : "" );
			break;

		case TEAM_RED:
			Com_Printf( "%2d " S_COLOR_RED "R   " S_COLOR_WHITE "%s" S_COLOR_WHITE "%s\n", i,
				ci->name, (ci->botSkill != -1) ? " (bot)" : "" );
			break;

		case TEAM_BLUE:
			Com_Printf( "%2d " S_COLOR_BLUE "B   " S_COLOR_WHITE "%s" S_COLOR_WHITE "%s\n", i,
				ci->name, (ci->botSkill != -1) ? " (bot)" : "" );
			break;

		default:
		case TEAM_SPECTATOR:
			Com_Printf( "%2d " S_COLOR_YELLOW "S   " S_COLOR_WHITE "%s" S_COLOR_WHITE "%s\n", i, ci->name, (ci->botSkill != -1) ? " (bot)" : "" );
			break;
		}

		count++;
	}

	Com_Printf( "Listed %2d clients\n", count );
}


static void CG_TellTarget_f( void ) {
	int		clientNum;
	char	command[MAX_SAY_TEXT+10];
	char	message[MAX_SAY_TEXT];

	clientNum = CG_CrosshairPlayer();
	if ( clientNum == -1 ) {
		return;
	}

	trap->Cmd_Args( message, sizeof(message) );
	Com_sprintf( command, sizeof(command), "tell %i %s", clientNum, message );
	trap->SendClientCommand( command );
}

static void CG_TellAttacker_f( void ) {
	int		clientNum;
	char	command[MAX_SAY_TEXT + 10];
	char	message[MAX_SAY_TEXT];

	clientNum = CG_LastAttacker();
	if ( clientNum == -1 ) {
		return;
	}

	trap->Cmd_Args( message, sizeof(message) );
	Com_sprintf( command, sizeof(command), "tell %i %s", clientNum, message );
	trap->SendClientCommand( command );
}

/*
==================
CG_StartOrbit_f
==================
*/

static void CG_StartOrbit_f( void ) {
	char var[MAX_TOKEN_CHARS];

	trap->Cvar_VariableStringBuffer( "developer", var, sizeof( var ) );
	if ( !atoi(var) ) {
		return;
	}
	if (cg_cameraOrbit.value != 0) {
		trap->Cvar_Set ("cg_cameraOrbit", "0");
		trap->Cvar_Set("cg_thirdPerson", "0");
	} else {
		trap->Cvar_Set("cg_cameraOrbit", "5");
		trap->Cvar_Set("cg_thirdPerson", "1");
		trap->Cvar_Set("cg_thirdPersonAngle", "0");
		trap->Cvar_Set("cg_thirdPersonRange", "100");
	}
}

void CG_SiegeBriefingDisplay(int team, int dontshow);
static void CG_SiegeBriefing_f(void)
{
	int team;

	if (cgs.gametype != GT_SIEGE)
	{ //Cannot be displayed unless in this gametype
		return;
	}

	team = cg.predictedPlayerState.persistant[PERS_TEAM];

	if (team != SIEGETEAM_TEAM1 &&
		team != SIEGETEAM_TEAM2)
	{ //cannot be displayed if not on a valid team
		return;
	}

	CG_SiegeBriefingDisplay(team, 0);
}

static void CG_SiegeCvarUpdate_f(void)
{
	int team;

	if (cgs.gametype != GT_SIEGE)
	{ //Cannot be displayed unless in this gametype
		return;
	}

	team = cg.predictedPlayerState.persistant[PERS_TEAM];

	if (team != SIEGETEAM_TEAM1 &&
		team != SIEGETEAM_TEAM2)
	{ //cannot be displayed if not on a valid team
		return;
	}

	CG_SiegeBriefingDisplay(team, 1);
}

static void CG_SiegeCompleteCvarUpdate_f(void)
{
	if (cgs.gametype != GT_SIEGE)
	{ //Cannot be displayed unless in this gametype
		return;
	}

	// Set up cvars for both teams
	CG_SiegeBriefingDisplay(SIEGETEAM_TEAM1, 1);
	CG_SiegeBriefingDisplay(SIEGETEAM_TEAM2, 1);
}

static void CG_NiceShot_f(void)
{
	int myTime;
	myTime = cg.time - cgs.levelStartTime;
	if( *cg_demoData.string ) Com_sprintf( cg_demoData.string, sizeof( cg_demoData.string ), "%s,%0.2d%0.2d", cg_demoData.string, (int)( myTime / 60000 ), (int)( ( myTime / 1000 ) % 60 ) );
	else Com_sprintf( cg_demoData.string, sizeof( cg_demoData.string ), "%0.2d%0.2d", (int)( myTime / 60000 ), (int)( ( myTime / 1000 ) % 60 ) );
	trap->Cvar_Set( "cg_demoData", cg_demoData.string );
	Com_Printf( "Nice shot!\n" );
}

void _mkdir(char *);
/*
============
FS_CreatePath

Creates any directories needed to store the given filename
============
*/
static qboolean FS_CreatePath2 (char *OSPath) {
	char	*ofs;
	
	// make absolutely sure that it can't back up the path
	// FIXME: is c: allowed???
	if ( strstr( OSPath, ".." ) || strstr( OSPath, "::" ) ) {
		//Com_Printf( "WARNING: refusing to create relative path \"%s\"\n", OSPath );
		return qtrue;
	}

	for (ofs = OSPath+1 ; *ofs ; ofs++) {
		if (*ofs == PATH_SEP) {	
			// create the directory
			*ofs = 0;
			_mkdir (OSPath);
			*ofs = PATH_SEP;
		}
	}
	return qfalse;
}

static void FS_CopyFile2( char *fromOSPath, char *toOSPath ) {
	FILE	*f;
	int		len;
	byte	*buf;

	Com_Printf( "copy %s to %s\n", fromOSPath, toOSPath );

	/*if (strstr(fromOSPath, "journal.dat") || strstr(fromOSPath, "journaldata.dat")) {
		Com_Printf( "Ignoring journal files\n");
		return;
	}*/

	f = fopen( fromOSPath, "rb" );
	if ( !f ) {
		return;
	}
	fseek (f, 0, SEEK_END);
	len = ftell (f);
	fseek (f, 0, SEEK_SET);

	// we are using direct malloc instead of Z_Malloc here, so it
	// probably won't work on a mac... Its only for developers anyway...
	buf = malloc( len );
	if (fread( buf, 1, len, f ) != len)
		;//Com_Error( ERR_FATAL, "Short read in FS_Copyfiles()\n" );
	fclose( f );

	if( FS_CreatePath( toOSPath ) ) {
		return;
	}

	f = fopen( toOSPath, "wb" );
	if ( !f ) {
		return;
	}
	if (fwrite( buf, 1, len, f ) != len)
		;//Com_Error( ERR_FATAL, "Short write in FS_Copyfiles()\n" );
	fclose( f );
	free( buf );
}

/*
===========
FS_Remove

===========
*/
static void FS_Remove( const char *osPath ) {
	remove( osPath );
}

void CG_FixDemo_f(void)
{
	char myDemoName[MAX_STRING_CHARS];
	char path[MAX_QPATH];
	trap->Cvar_VariableStringBuffer( "fs_game", path, sizeof( path ) );
	if( !*path ) Q_strncpyz( path, "base", sizeof( path ) );
	trap->Cvar_VariableStringBuffer( "cg_demoName", myDemoName, sizeof( myDemoName ) );
	if( *cg_demoData.string )
		FS_CopyFile( va( "%s/demos/%s.dm_26", path, myDemoName ), va( "%s/demos/%s.dm_26", path, cg_demoData.string ) );
	FS_Remove( va( "%s/demos/%s.dm_26", path, myDemoName ) );
	trap->Cvar_Set( "cg_demoData", "" );
	Com_Printf( "Demo name fixed.\n" );
}

static void CG_LoadHud_f( void ) {
	const char *hudSet = cg_hudFiles.string;
	if ( hudSet[0] == '\0' ) {
		hudSet = "ui/jahud.txt";
	}

	String_Init();
	Menu_Reset();
	CG_LoadMenus( hudSet );
}

typedef struct consoleCommand_s {
	const char	*cmd;
	void		(*func)(void);
} consoleCommand_t;

int cmdcmp( const void *a, const void *b ) {
	return Q_stricmp( (const char *)a, ((consoleCommand_t*)b)->cmd );
}

static consoleCommand_t	commands[] = {
	{ "+scores",					CG_ScoresDown_f },
	{ "-scores",					CG_ScoresUp_f },
	{ "briefing",					CG_SiegeBriefing_f },
	{ "clientlist",					CG_ClientList_f },
	{ "fixdemo", CG_FixDemo_f },
	{ "forcenext",					CG_NextForcePower_f },
	{ "forceprev",					CG_PrevForcePower_f },
	{ "invnext",					CG_NextInventory_f },
	{ "invprev",					CG_PrevInventory_f },
	{ "loaddeferred",				CG_LoadDeferredPlayers },
	{ "loadhud",					CG_LoadHud_f },
	{ "nextframe",					CG_TestModelNextFrame_f },
	{ "nextskin",					CG_TestModelNextSkin_f },
	{ "niceshot", CG_NiceShot_f },
	{ "prevframe",					CG_TestModelPrevFrame_f },
	{ "prevskin",					CG_TestModelPrevSkin_f },
	{ "siegeCompleteCvarUpdate",	CG_SiegeCompleteCvarUpdate_f },
	{ "siegeCvarUpdate",			CG_SiegeCvarUpdate_f },
	{ "sizedown",					CG_SizeDown_f },
	{ "sizeup",						CG_SizeUp_f },
	{ "startOrbit",					CG_StartOrbit_f },
	{ "tcmd",						CG_TargetCommand_f },
	{ "tell_attacker",				CG_TellAttacker_f },
	{ "tell_target",				CG_TellTarget_f },
	{ "testgun",					CG_TestGun_f },
	{ "testmodel",					CG_TestModel_f },
	{ "varMath", CG_VarMath_f },
	{ "viewpos",					CG_Viewpos_f },
	{ "weapnext",					CG_NextWeapon_f },
	{ "weapon",						CG_Weapon_f },
	{ "weaponclean",				CG_WeaponClean_f },
	{ "weapprev",					CG_PrevWeapon_f },
};

static const size_t numCommands = ARRAY_LEN( commands );

/*
=================
CG_ConsoleCommand

The string has been tokenized and can be retrieved with
Cmd_Argc() / Cmd_Argv()
=================
*/
qboolean CG_ConsoleCommand( void ) {
	consoleCommand_t	*command = NULL;

	command = (consoleCommand_t *)Q_LinearSearch( CG_Argv( 0 ), commands, numCommands, sizeof( commands[0] ), cmdcmp );

	if ( !command || !command->func )
		return qfalse;

	command->func();
	return qtrue;
}

static const char *gcmds[] = {
	"addbot",
	"callteamvote",
	"callvote",
	"duelteam",
	"follow",
	"follownext",
	"followprev",
	"forcechanged",
	"give",
	"god",
	"kill",
	"levelshot",
	"loaddefered",
	"noclip",
	"notarget",
	"NPC",
	"say",
	"say_team",
	"setviewpos",
	"siegeclass",
	"stats",
	//"stopfollow",
	"team",
	"teamtask",
	"teamvote",
	"tell",
	"voice_cmd",
	"vote",
	"where",
	"zoom"
};
static const size_t numgcmds = ARRAY_LEN( gcmds );

/*
=================
CG_InitConsoleCommands

Let the client system know about all of our commands
so it can perform tab completion
=================
*/
void CG_InitConsoleCommands( void ) {
	size_t i;

	for ( i = 0; i < numCommands; i++ )
		trap->AddCommand( commands[i].cmd );

	//
	// the game server will interpret these commands, which will be automatically
	// forwarded to the server after they are not recognized locally
	//
	for( i = 0; i < numgcmds; i++ )
		trap->AddCommand( gcmds[i] );
}
