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

void CG_ReadableSize ( char *buf, int bufsize, int value ) {
	if (value > 1024*1024*1024 ) { // gigs
		Com_sprintf( buf, bufsize, "%d", value / (1024*1024*1024) );
		Com_sprintf( buf+strlen(buf), bufsize-strlen(buf), ".%02d GB", 
			(value % (1024*1024*1024))*100 / (1024*1024*1024) );
	} else if (value > 1024*1024 ) { // megs
		Com_sprintf( buf, bufsize, "%d", value / (1024*1024) );
		Com_sprintf( buf+strlen(buf), bufsize-strlen(buf), ".%02d MB", 
			(value % (1024*1024))*100 / (1024*1024) );
	} else if (value > 1024 ) { // kilos
		Com_sprintf( buf, bufsize, "%d KB", value / 1024 );
	} else { // bytes
		Com_sprintf( buf, bufsize, "%d bytes", value );
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

struct FtpFile {
	char filename[MAX_STRING_CHARS];
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

char testpath[MAX_STRING_CHARS] = "";

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
	if(testpath[0] == '\0') {
		trap->Cvar_VariableStringBuffer("fs_homepath", testpath, sizeof(testpath));
		Q_strcat(testpath, sizeof(testpath), "/base/");
	}
	if(base[0] == '\0') {
		trap->Cvar_VariableStringBuffer( "g_dlURL", base, MAX_STRING_CHARS );
		if (base[0] == '\0') {
			trap->Cvar_VariableStringBuffer( "cl_wwwBaseUrl", base, MAX_STRING_CHARS );
		}
	}
	/*if (base[strlen(base)-1] == '\\' || base[strlen(base)-1] == '/') {
		base[strlen(base)-1] = 0;
	}*/
	if ( refPaks[0] == '\0' ) {
		trap->Cvar_VariableStringBuffer( "sv_referencedPaks", refPaks, sizeof( refPaks ) );
	}
	if(referer[0] == '\0') {
		char sv_address[MAX_STRING_CHARS];
		trap->Cvar_VariableStringBuffer("cl_currentServerAddress", sv_address, sizeof(sv_address));
		Com_sprintf( referer, sizeof(referer), "jka://%s", sv_address);
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

    return needsrestart;
}

int ListFiles ( void ) {
	char value[MAX_STRING_CHARS];

	CG_InitThreads(); // we gotta init threads before we call any pthread stuff on linux/mac

	trap->Cvar_VariableStringBuffer( "cl_checksumProblem", value, sizeof( value ) );
	if ( atoi( value ) == 1 ) {
		Com_Printf("DOWNLOAD: cl_checksumProblem is 1, bailing out\n");
		trap->Cvar_Set( "cl_checksumProblem", "0" );
		return 0;
	}
	trap->Cvar_VariableStringBuffer( "cl_triedDownload", value, sizeof( value ) );
	if ( atoi( value ) == 1 ) {
		Com_Printf("DOWNLOAD: cl_triedDownload is 1, bailing out\n");
		trap->Cvar_Set( "cl_triedDownload", "0" );
		return 0;
	}
	trap->Cvar_VariableStringBuffer( "cl_wwwDownload", value, sizeof( value ) );
	if ( atoi( value ) == 0 ) {
		Com_Printf("DOWNLOAD: cl_wwwDownload is 0, bailing out\n");
		return 0;
	}
	trap->Cvar_VariableStringBuffer( "sv_referencedPakNames", value, sizeof( value ) );

	return runthrostringpls( value );
}

void *progress_callback (void* fp, double dltotal, double dlnow, double ultotal, double ulnow)	
{
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
		Com_sprintf( ftpfile->filename, sizeof(ftpfile->filename), "%s\\%s", ftpfile->basepath, head+42 );
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

    Com_sprintf( ftpfile.filename, sizeof(ftpfile.filename), "%s%s", testpath, file ); /* name to store the file as if succesful */
	strcpy( ftpfile.basepath, testpath );
    ftpfile.stream = NULL;
	ftpfile.realfilename[0] = 0;

	strcpy( mapname, file );

	curl = curl_easy_init();
	if(curl) 
	{
		char url[MAX_STRING_CHARS];
		Com_sprintf( url, sizeof( url ), "%sbase/%s", base, file );
		curl_easy_setopt(curl, CURLOPT_URL, url);

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
	
	if( cgs.is_downloading == 1 ) cgs.is_downloading = 2;

	return 0;
}

#endif

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
	char homepath[MAX_CVAR_VALUE_STRING];
	trap->Cvar_VariableStringBuffer( "cg_demoName", myDemoName, sizeof( myDemoName ) );
	if ( myDemoName[0] == '\0' ) return;
	trap->Cvar_VariableStringBuffer( "fs_game", path, sizeof( path ) );
	if( !*path ) Q_strncpyz( path, "base", sizeof( path ) );
	trap->Cvar_VariableStringBuffer( "fs_homepath", homepath, sizeof( homepath ) );
	if ( homepath[0] != '\0' ) Q_strcat( homepath, sizeof( homepath ), "\\" );
	if( *cg_demoData.string )
		FS_CopyFile( va( "%s%s/demos/%s.dm_26", homepath, path, myDemoName ), va( "%s%s/demos/%s - %s.dm_26", homepath, path, cg_demoData.string, myDemoName ) );
	FS_Remove( va( "%s%s/demos/%s.dm_26", homepath, path, myDemoName ) );
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

typedef struct bitInfo_S {
	const char	*string;
} bitInfo_T;

static bitInfo_T strafeTweaks[] = {
	{"Original style"},//0
	{"Updated style"},//1
	{"Cgaz style"},//2
	{"Warsow style"},//3
	{"Sound"},//4
	{"W"},//5
	{"WA"},//6
	{"WD"},//7
	{"A"},//8
	{"D"},//9
	{"Rear"},//10
	{"Center"},//11
	{"Accel bar"},//12
	{"Weze style"},//13
	{"Line Crosshair"}//13
};
static const int MAX_STRAFEHELPER_TWEAKS = ARRAY_LEN( strafeTweaks );

extern void CG_ClearThirdPersonDamp(void);
void CG_StrafeHelper_f( void ) {
	if ( trap->Cmd_Argc() == 1 ) {
		int i = 0;
		for ( i = 0; i < MAX_STRAFEHELPER_TWEAKS; i++ ) {
			if ( (cg_strafeHelper.integer & (1 << i)) ) {
				Com_Printf( "%2d [X] %s\n", i, strafeTweaks[i].string );
			}
			else {
				Com_Printf( "%2d [ ] %s\n", i, strafeTweaks[i].string );
			}
		}
		return;
	}
	else {
		char arg[8] = { 0 };
		int index;
		const uint32_t mask = (1 << MAX_STRAFEHELPER_TWEAKS) - 1;

		trap->Cmd_Argv( 1, arg, sizeof(arg) );
		index = atoi( arg );

		if ( index < 0 || index >= MAX_STRAFEHELPER_TWEAKS ) {
			Com_Printf( "strafeHelper: Invalid range: %i [0, %i]\n", index, MAX_STRAFEHELPER_TWEAKS - 1 );
			return;
		}

		if ((index == 0 || index == 1 || index == 2 || index == 3 || index == 13)) { //Radio button these options
			//Toggle index, and make sure everything else in this group (0,1,2,3,13) is turned off
			int groupMask = (1 << 0) + (1 << 1) + (1 << 2) + (1 << 3) + (1 << 13);
			int value = cg_strafeHelper.integer;

			groupMask &= ~(1 << index); //Remove index from groupmask
			value &= ~(groupMask); //Turn groupmask off
			value ^= (1 << index); //Toggle index item

			trap->Cvar_Set("cg_strafeHelper", va("%i", value));
		}
		else {
			trap->Cvar_Set("cg_strafeHelper", va("%i", (1 << index) ^ (cg_strafeHelper.integer & mask)));
		}
		trap->Cvar_Update( &cg_strafeHelper );

		Com_Printf( "%s %s^7\n", strafeTweaks[index].string, ((cg_strafeHelper.integer & (1 << index))
			? "^2Enabled" : "^1Disabled") );
	}

	CG_ClearThirdPersonDamp();
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
	{ "strafeHelper",				CG_StrafeHelper_f },
	{ "tcmd",						CG_TargetCommand_f },
	{ "tell_attacker",				CG_TellAttacker_f },
	{ "tell_target",				CG_TellTarget_f },
	{ "testgun",					CG_TestGun_f },
	{ "testmodel",					CG_TestModel_f },
	{ "varMath",					CG_VarMath_f },
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
