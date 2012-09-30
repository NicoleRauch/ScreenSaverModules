#define INCL_DOS
#define INCL_DOSERRORS
#define INCL_WIN
#define INCL_GPI
#include <os2.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <process.h>
#include <math.h>


#include "galaxy.h"




#define MODULEVERSION		0x00010005
#define STACKSIZE		32000
#define SAVER_NAME_MAXLEN	32
#define ZORDERTIMERSTEP 	30000L
#define FUNCTION_CONFIGURE	1
#define FUNCTION_STARTSAVER	2
#define FUNCTION_STOPSAVER	3
#define FUNCTION_QUERYNAME	4
#define FUNCTION_QUERYENABLED	5
#define FUNCTION_SETENABLED	6
#define FUNCTION_SETINIFILENAME 7
#define CONFIGURATION_DEFAULT_ANIMATIONSPEED 4	/* 0=slow .. 4=fast */

#define CONFIGURATION_MINIMUM_GRAVITY  		   1
#define CONFIGURATION_DEFAULT_GRAVITY  		   3
#define CONFIGURATION_MAXIMUM_GRAVITY  	   	20

#define CONFIGURATION_MINIMUM_STARS 		   0
#define CONFIGURATION_DEFAULT_STARS     	   15
#define CONFIGURATION_MAXIMUM_STARS       	40

#define CNT_STARS 42
#define MAX_NAMESIZE 22


char starNames[CNT_STARS][MAX_NAMESIZE]=
          { "ALPHA CENTAURI"          ,
            "BARNARDS STAR"           ,
            "WOLF 359"                ,
            "LUYTEN 726-8"            ,
            "LALANDE 21185"           ,
            "SIRIUS"                  ,
            "ROSS 154"                ,
            "ROSS 248"                ,
            "EPSILON ERIDANI"         ,
            "ROSS 128"                ,
            "61 CYGNI"                ,
            "LUYTEN 789-6"            ,
            "PROCYON"                 ,
            "EPSILON INDI"            ,
            "EPSILON 2398"            ,
            "GROOMBRIDGE 34"          ,
            "TAU CETI"                ,
            "LACAILLE 9352"           ,
            "BD+5ø1668"               ,
            "LUYTEN 725-32"           ,
            "LACAILLE 8760"           ,
            "KAPTEYNS STAR"           ,
            "KRUEGER 60"              ,
            "ROSS 614"                ,
            "BD-12ø4523"              ,
            "VAN MAANENS STAR"        ,
            "WOLF 424"                ,
            "GROOMBRIDGE 1618"        ,
            "CD-37ø15492"             ,
            "CD-46ø11540"             ,
            "BD+20ø2465"              ,
            "CD-44ø11909"             ,
            "CD-49ø13515"             ,
            "AOE 17415-6"             ,
            "ROSS 780"                ,
            "LALANDE 25372"           ,
            "CC 658"                  ,
            "SIGMA ERIDANI"           ,
            "70 OPHIUCHI"             ,
            "ATAIR"                   ,
            "BD+43ø4305"              ,
            "AC 79ø3888"
          };


void	EXPENTRY SAVER_PROC(int function, HAB _hab, HWND hwndOwner, char *appname, void *buffer);
static	MRESULT EXPENTRY SaverWindowProc(HWND hwnd, ULONG msg, MPARAM mp1, MPARAM mp2);
static	MRESULT EXPENTRY ConfigureDlgProc(HWND hwnd, ULONG msg, MPARAM mp1, MPARAM mp2);
#if defined(__IBMC__) || defined(__EMX__)
static	void	_Optlink draw_thread(void *args);
static	void	_Optlink priority_thread(void *args);
#elif defined(__BORLANDC__)
static	void	_USERENTRY draw_thread(void *args);
static	void	_USERENTRY priority_thread(void *args);
#endif
static	void	load_configuration_data(void);
static	void	write_profile_data(void);
static	ULONG	query_profile_data(void);




ULONG	MODULESOURCE_VERSION = 0x00010005;	/* EXPORTED: ScreenSaver 1.5 module source code (do not change) */

static	LONG	screenSizeX = 0;	  
static	LONG	screenSizeY = 0;	  
static	HWND	hwndSaver = NULLHANDLE; 	/* saver window handle             */
static	HMODULE hmodDLL = NULLHANDLE;		/* saver module dll handle         */
static	char	*application_name;		   /* name of ScreenSaver app         */
static	TID	tidDraw;			            /* drawing-thread ID               */
static	TID	tidPriority;		      	/* priority regulation thread ID   */
static	HPS	hps;				            /* presentation space handle       */
static	HAB	hab;			            	/* anchor block handle             */
static	BOOL	low_priority = TRUE;	    	/* low-priority flag               */
static	BOOL	configuration_data_loaded = FALSE; /* config data loaded flag */
static	volatile BOOL stop_draw_thread; 	        /* stop flag               */
static	char	modulename[SAVER_NAME_MAXLEN+1];   /* module name buffer      */
static	volatile BOOL stop_priority_thread = FALSE;
static	char	ini_filename[CCHMAXPATH] = "";     /* name of INI file if other than OS2.INI */

static	struct	_configuration_data {
	ULONG	version;
	BOOL	enabled;
   BOOL  tracename;
   BOOL  show3d;
   int      gravity;
   int      whitestar_count;
   int      redstar_count;
   int      bluestar_count;
   int      yellowstar_count;
} configuration_data;



/*
	SAVER_PROC
	This is the entry point into the saver module that is called by
	the ScreenSaver program.
	There should be no reason to alter the code.
	Depending on the requested function, the following tasks
	are performed:
	* set the name of the INI file to store and query settings
	* call the configuration dialog of the saver module
	* copy the name of the saver module into the supplied buffer
	* tell if the saver module is enabled
	* set the "enabled" state of the saver module
	* start the saver
	* stop the saver
	Note that before any processing is done, module configuration data is
	loaded from the INI-files.
*/
void	EXPENTRY SAVER_PROC(int function, HAB _hab, HWND hwndOwner, char *appname, void *buffer)
{
#if defined(__BORLANDC__)
	extern ULONG __os2hmod;
	hmodDLL = __os2hmod;
#endif
	hab = _hab;
	application_name = appname;
	/* load all configuration data from INI-file */
	if(function != FUNCTION_SETINIFILENAME)
		load_configuration_data();
	switch(function){
	case FUNCTION_SETINIFILENAME:
		if(buffer != NULL)
			strcpy(ini_filename, (char *)buffer);
		else
			strcpy(ini_filename, "");
		return;
	case FUNCTION_CONFIGURE:
		/* call the configuration dialog */
		WinDlgBox(HWND_DESKTOP, hwndOwner, ConfigureDlgProc,
		  hmodDLL, IDD_CONFIGURE, application_name);
		return;
	case FUNCTION_STARTSAVER:
		/* start the saver */
		/* get "low priority" state from supplied buffer (BOOL *) */
		low_priority = *((BOOL *)buffer);
		/* query size of screen */
		screenSizeX = WinQuerySysValue(HWND_DESKTOP, SV_CXSCREEN);
		screenSizeY = WinQuerySysValue(HWND_DESKTOP, SV_CYSCREEN);
		/* register window class for the saver window */
		WinRegisterClass(hab, (PSZ)modulename,
		  (PFNWP)SaverWindowProc, 0, 0);
		/* create the saver window */
		hwndSaver = WinCreateWindow(HWND_DESKTOP, (PSZ)modulename,
		  (PSZ)NULL, WS_VISIBLE, 0, 0, screenSizeX, screenSizeY,
		  HWND_DESKTOP, HWND_TOP, 0, NULL, NULL);
		return;
	case FUNCTION_STOPSAVER:
		/* stop the saver */
		if(low_priority){
			stop_priority_thread = TRUE;
			DosSetPriority(PRTYS_THREAD, PRTYC_REGULAR, 0, tidPriority);
			DosSetPriority(PRTYS_THREAD, PRTYC_REGULAR, 0, tidDraw);
			DosWaitThread(&tidPriority, DCWW_WAIT);
/*			  DosKillThread(tidPriority);	***** kills thread 1 under OS/2 2.11 */
		}
		/* tell drawing-thread to stop */
		stop_draw_thread = TRUE;
		if(DosWaitThread(&tidDraw, DCWW_NOWAIT) == ERROR_THREAD_NOT_TERMINATED){
			/* if priority of drawing-thread was set to idle time */
			/* priority, set it back to normal value */
			DosSetPriority(PRTYS_THREAD, PRTYC_REGULAR, PRTYD_MAXIMUM, tidDraw);
			/* wait until drawing-thread has ended */
			DosWaitThread(&tidDraw, DCWW_WAIT);
		}

		if(hwndSaver != NULLHANDLE){
			/* move saver window to front */
			WinSetWindowPos(hwndSaver, HWND_TOP,
			  0, 0, 0, 0, SWP_ZORDER);
			/* destroy saver window */
			WinDestroyWindow(hwndSaver);
			hwndSaver = NULLHANDLE;
		}
		return;
	case FUNCTION_QUERYNAME:
		/* copy module name to supplied buffer (CHAR *) */
		strcpy(buffer, modulename);
		return;
	case FUNCTION_QUERYENABLED:
		/* copy "enabled" state to supplied buffer (BOOL *) */
		*((BOOL *)buffer) = configuration_data.enabled;
		return;
	case FUNCTION_SETENABLED:
		/* get new "enabled" state from supplied buffer (BOOL *) */
		configuration_data.enabled = *((BOOL *)buffer);
		write_profile_data();
		return;
	}

	/* illegal function request */
	WinAlarm(HWND_DESKTOP, WA_ERROR);
	return;
}

#if !defined(__BORLANDC__)
/*
	_DLL_InitTerm
	This procedure is called at DLL initialization and termination.
	There should be no reason to alter the code.
*/
ULONG	_DLL_InitTerm(HMODULE hmod, ULONG flag)
{
	switch(flag){
	case 0: /* initializing DLL */
		hmodDLL = hmod;
		return 1;
	case 1: /* terminating DLL */
		return 1;
	default:
		/* return error */
		return 0;
	}
}
#endif

/*
	ConfigureDlgProc
	This is the dialog procedure for the module configuration dialog.
	The dialog contains a check box for enabling/disabling the module
	and two push buttons ("OK" and "Cancel") to close/cancel the dialog.
	Since version 1.2, it contains a slider to set the animation speed;
	if you don't want this slider, change the '#if 0' line in the
	WM_INITDLG part of this dialog procedure.

	This is enough for simple saver modules, but can easily be expanded
	for more saver modules that need more settings.
*/
MRESULT EXPENTRY ConfigureDlgProc(HWND hwnd, ULONG msg, MPARAM mp1, MPARAM mp2)
{
	char	buf[sizeof(modulename)+20];
	static	HWND	hwndEnabled;
	static	HWND	hwndGravity;
	static	HWND  hwndWhiteStars,hwndRedStars;
	static	HWND	hwndBlueStars,hwndYellowStars;
	static	HWND	hwndTraceName;
	static	HWND	hwndShow3d;

   switch(msg){
	case WM_INITDLG:
		/* set titlebar of the dialog window */
		/* to "MODULENAME configuration" */
		strcpy(buf, modulename);
		strcat(buf, " configuration");
		WinSetWindowText(hwnd, (PSZ)buf);

		/* get window handles of the dialog controls */
		/* and set initial state of the controls     */
		hwndEnabled = WinWindowFromID(hwnd, IDC_ENABLED);
   	WinSendMsg(hwndEnabled, BM_SETCHECK,
	   MPFROMSHORT(configuration_data.enabled), MPVOID);

      
		hwndEnabled = WinWindowFromID(hwnd, IDC_ENABLED);
		WinSendMsg(hwndEnabled, BM_SETCHECK,MPFROMSHORT(configuration_data.enabled), MPVOID);

		hwndTraceName = WinWindowFromID(hwnd, IDC_TRACENAME);
		WinSendMsg(hwndTraceName, BM_SETCHECK,MPFROMSHORT(configuration_data.tracename), MPVOID);

		hwndShow3d = WinWindowFromID(hwnd, IDC_3D);
		WinSendMsg(hwndShow3d, BM_SETCHECK,MPFROMSHORT(configuration_data.show3d), MPVOID);

		hwndGravity = WinWindowFromID(hwnd, IDC_GRAVITY);
		WinSendMsg(hwndGravity, SPBM_SETLIMITS, (MPARAM)CONFIGURATION_MAXIMUM_GRAVITY, (MPARAM)CONFIGURATION_MINIMUM_GRAVITY);
		WinSendMsg(hwndGravity, SPBM_SETCURRENTVALUE, MPFROMSHORT(configuration_data.gravity), MPVOID);

		hwndWhiteStars = WinWindowFromID(hwnd, IDC_WHITESTARS);
		WinSendMsg(hwndWhiteStars, SPBM_SETLIMITS, (MPARAM)CONFIGURATION_MAXIMUM_STARS, (MPARAM)CONFIGURATION_MINIMUM_STARS);
		WinSendMsg(hwndWhiteStars, SPBM_SETCURRENTVALUE, MPFROMSHORT(configuration_data.whitestar_count), MPVOID);

		hwndRedStars = WinWindowFromID(hwnd, IDC_REDSTARS);
		WinSendMsg(hwndRedStars, SPBM_SETLIMITS, (MPARAM)CONFIGURATION_MAXIMUM_STARS, (MPARAM)CONFIGURATION_MINIMUM_STARS);
		WinSendMsg(hwndRedStars, SPBM_SETCURRENTVALUE, MPFROMSHORT(configuration_data.redstar_count), MPVOID);

		hwndBlueStars = WinWindowFromID(hwnd, IDC_BLUESTARS);
		WinSendMsg(hwndBlueStars, SPBM_SETLIMITS, (MPARAM)CONFIGURATION_MAXIMUM_STARS, (MPARAM)CONFIGURATION_MINIMUM_STARS);
		WinSendMsg(hwndBlueStars, SPBM_SETCURRENTVALUE, MPFROMSHORT(configuration_data.bluestar_count), MPVOID);

		hwndYellowStars = WinWindowFromID(hwnd, IDC_YELLOWSTARS);
		WinSendMsg(hwndYellowStars, SPBM_SETLIMITS, (MPARAM)CONFIGURATION_MAXIMUM_STARS, (MPARAM)CONFIGURATION_MINIMUM_STARS);
		WinSendMsg(hwndYellowStars, SPBM_SETCURRENTVALUE, MPFROMSHORT(configuration_data.yellowstar_count), MPVOID);

      /* return FALSE since we did not change the focus */
		return (MRESULT)FALSE;
	case WM_COMMAND:
		switch(SHORT1FROMMP(mp1)){
		case IDC_OK:
			/* OK button was pressed. query the control settings */
			configuration_data.enabled   = SHORT1FROMMR(WinSendMsg(hwndEnabled, BM_QUERYCHECK, MPVOID, MPVOID));
			configuration_data.tracename = SHORT1FROMMR(WinSendMsg(hwndTraceName, BM_QUERYCHECK, MPVOID, MPVOID));
			configuration_data.show3d    = SHORT1FROMMR(WinSendMsg(hwndShow3d, BM_QUERYCHECK, MPVOID, MPVOID));

         WinSendMsg(hwndGravity,     SPBM_QUERYVALUE, MPFROMP(&configuration_data.gravity),          MPFROM2SHORT(0, SPBQ_DONOTUPDATE));
         WinSendMsg(hwndWhiteStars,  SPBM_QUERYVALUE, MPFROMP(&configuration_data.whitestar_count),  MPFROM2SHORT(0, SPBQ_DONOTUPDATE));
         WinSendMsg(hwndRedStars,    SPBM_QUERYVALUE, MPFROMP(&configuration_data.redstar_count),    MPFROM2SHORT(0, SPBQ_DONOTUPDATE));
         WinSendMsg(hwndBlueStars,   SPBM_QUERYVALUE, MPFROMP(&configuration_data.bluestar_count),   MPFROM2SHORT(0, SPBQ_DONOTUPDATE));
         WinSendMsg(hwndYellowStars, SPBM_QUERYVALUE, MPFROMP(&configuration_data.yellowstar_count), MPFROM2SHORT(0, SPBQ_DONOTUPDATE));


			/* write all configuration data to INI-file */
			write_profile_data();

			/* end dialog */
			WinDismissDlg(hwnd, TRUE);
			return (MRESULT)0;
		case IDC_CANCEL:
			/* dialog was cancelled; end it */
			WinDismissDlg(hwnd, FALSE);
			return (MRESULT)0;
		default:
			return (MRESULT)0;
		}
	}
	return WinDefDlgProc(hwnd, msg, mp1, mp2);
}

/*
	SaverWindowProc
	This is the window procedure of the screen-size window that is
	created when the saver starts.
	There should be no reason to alter the code.
	Note that we do not process WM_PAINT messages. They are forwarded to
	the default window procedure, which just validates the window area
	and does no drawing. All drawing to the window should be done in
	the drawing-thread. Therefore, if you want to blank the screen before
	drawing on it for instance, issue a WinFillRect call at the beginning
	of your drawing-thread.
*/
MRESULT EXPENTRY SaverWindowProc(HWND hwnd, ULONG msg, MPARAM mp1, MPARAM mp2)
{
	switch(msg){
	case WM_CREATE:
		/* reset the "stop" flag */
		stop_draw_thread = FALSE;
		/* store window handle */
		hwndSaver = hwnd;
		/* get presentation space */
		hps = WinGetPS(hwnd);
		/* start the drawing-thread */
/*
		$$$$$ note $$$$$
		Some compilers use another parameter ordering for
		_beginthread. The _beginthread call below works with EMX,
		ICC and BCC. Check your compiler docs for other compilers.
*/
#if defined(__BORLANDC__)
		/* for Borland C++ */
		tidDraw = _beginthread(draw_thread, STACKSIZE, NULL);
#elif defined(__EMX__) || defined(__IBMC__)
		/* for EMX and ICC */
		tidDraw = _beginthread(draw_thread, NULL, STACKSIZE, NULL);
#endif
		/* create thread to control priority of drawing thread */
		if(low_priority){
			stop_priority_thread = FALSE;
			DosCreateThread(&tidPriority, (PFNTHREAD)priority_thread, 0, 2L, 1000);
		}
		/* create timer that moves the saver window to top regularly */
		WinStartTimer(hab, hwndSaver, IDT_ZORDERTIMER, ZORDERTIMERSTEP);
		return (MRESULT)FALSE;
	case WM_TIMER:
		if(SHORT1FROMMP(mp1) == IDT_ZORDERTIMER){
			/* move saver window to top */
			WinSetWindowPos(hwnd, HWND_TOP, 0, 0, 0, 0, SWP_ZORDER);
			return (MRESULT)0;
		}
		break;
	case WM_DESTROY:
		/* release the presentation space */
		WinReleasePS(hps);
		/* stop the z-order timer */
		WinStopTimer(hab, hwndSaver, IDT_ZORDERTIMER);
		break;
	case WM_PAINT:
		/* just validate the update area. all drawing is done */
		/* in the drawing-thread.                             */
		return WinDefWindowProc(hwnd, msg, mp1, mp2);
	}
	return WinDefWindowProc(hwnd, msg, mp1, mp2);
}

/*
	priority_thread
	This thread controls the priority of the drawing thread.
	With these changes, if a saver module runs on low priority (this is
	the default setting), it rises to normal priority twice a second
	for 0.1 seconds. This should solve the problem that, when very
	time-consuming processes were running, the module seemed not to become
	active at all (in fact it became active, but did not get enough CPU
	time to do its saver action).
	There should be no reason to alter the code.
*/
void	priority_thread(void *args)
{
	DosSetPriority(PRTYS_THREAD, PRTYC_TIMECRITICAL, 0, 0);
	for(;!stop_priority_thread;){
		int	i;
		DosSetPriority(PRTYS_THREAD, PRTYC_REGULAR, 0, tidDraw);
		DosSleep(100);
		DosSetPriority(PRTYS_THREAD, PRTYC_IDLETIME, 0, tidDraw);
		for(i=0;!stop_priority_thread && i<4;i++)
			DosSleep(100);
	}
}

/*
	load_configuration_data
	Load all module configuration data from the INI-file into the
	configuration_data structure, if not done already loaded.
*/
void	load_configuration_data(void)
{
	if(configuration_data_loaded == FALSE){
		/* data not loaded yet */
		ULONG	size;
		/* get name of the saver module (stored as resource string) */
		if(WinLoadString(hab, hmodDLL, IDS_MODULENAME,
		  SAVER_NAME_MAXLEN, (PSZ)modulename) == 0){
			/* resource string not found. indicate error by   */
			/* setting module name to empty string (the name  */
			/* "" is interpreted as an error by SSDLL.DLL and */
			/* the module does not show up in the list box).  */
			strcpy(modulename, "");
			return;
		}
		/* load data from INI-file. the key name is the name of the */
		/* saver module                                             */
		size = query_profile_data();
		if(size != sizeof(configuration_data) || configuration_data.version != MODULEVERSION){
			/* if entry is not found or entry has invalid size or */
			/* entry has wrong version number, create a new entry */
			/* with default values and write it to the INI-file   */

         configuration_data.version           = MODULEVERSION;
			configuration_data.enabled           = TRUE;
			configuration_data.tracename         = TRUE;
			configuration_data.show3d            = TRUE;
			configuration_data.gravity           = CONFIGURATION_DEFAULT_GRAVITY;
         configuration_data.whitestar_count   = CONFIGURATION_DEFAULT_STARS;
         configuration_data.redstar_count     = CONFIGURATION_DEFAULT_STARS;
         configuration_data.bluestar_count    = CONFIGURATION_DEFAULT_STARS;
         configuration_data.yellowstar_count  = CONFIGURATION_DEFAULT_STARS;         

			write_profile_data();
		}
		configuration_data_loaded = TRUE;
	}
}

/*
	write_profile_data
	Write configuration data to the selected INI file.
	There should be no reason to alter the code.
*/
void	write_profile_data()
{
	if(*ini_filename)
		return; 	

	PrfWriteProfileData(HINI_USER, (PSZ)application_name, (PSZ)modulename, (PSZ)&configuration_data, sizeof(configuration_data));
}

/*
	query_profile_data
	Read configuration data from the selected INI file.
	There should be no reason to alter the code.
*/
ULONG	query_profile_data()
{
	HINI	ini_file;
	BOOL	fSuccess;
	ULONG	size;

	if(*ini_filename){
		ini_file = PrfOpenProfile(hab, (PSZ)ini_filename);
		if(ini_file == NULLHANDLE)
			ini_file = HINI_USER;
	}else
		ini_file = HINI_USER;

	size = sizeof(configuration_data);
	fSuccess = PrfQueryProfileData(ini_file,
	  (PSZ)application_name, (PSZ)modulename,
	  (PSZ)&configuration_data, &size);

	if(ini_file != HINI_USER)
		PrfCloseProfile(ini_file);
	if(fSuccess == FALSE)
		return 0;
	else
		return size;
}

/*
	draw_thread
	This is the drawing-thread.
	You have a valid presentation space handle (hps), a valid window
	handle (hwndSaver) and the configuration_data structure is loaded.
	The screen size is stored in "screenSizeX" and "screenSizeY".
	IMPORTANT NOTE 1:
	You must check the "stop_draw_thread" flag regularly. If it is set,
	free all resources you have allocated and call _endthread (or just
	return) to end the drawing-thread.
	IMPORTANT NOTE 2:
	If the "low_priority" flag is NOT set (that means you run with
	regular priority, sharing CPU usage with other programs), you should
	call DosSleep(x) with "x" set at least to 1 as often as possible, to
	allow other programs to do their work. A screen saver should not eat
	up other program's CPU time!
	IMPORTANT NOTE 3:
	For some of the PM calls to work properly, your thread needs an
	own HAB and maybe even a message queue. You have to get and release
	both of them here if you use such PM calls.

	The following galaxy code is from the "Pyramids" module that comes
	with the ScreenSaver distribution.
	It selects a random color and a random point on the screen, then
	draws lines in the selected color from each corner of the screen
	to the selected point (looks somewhat like a pyramid).
	It remembers a number of points (this number can be set in the
	configuration dialog). Having filled the point memory, it redraws
	the "oldest" visible pyramid in black. This has the effect that more
	and more pixels on the screen get black, only a few constantly
	changing colored lines remain.
*/


#define  TNG_FONT "Sans Condensed"
LONG fontId = 1L;


void setTNGfont(HPS hps)
{
    LONG         cFonts;
    LONG         lTemp = 0L;
    CHAR         fontName[25];
    FATTRS       fattrs;
    SIZEF        sizfCharBox;

    fattrs.usRecordLength  = sizeof(FATTRS);
    fattrs.fsSelection     = 0;
    fattrs.lMatch          = 0L;
    fattrs.idRegistry      = 0;
    fattrs.usCodePage      = 850;
    fattrs.lMaxBaselineExt = 0L;
    fattrs.lAveCharWidth   = 0L;
    fattrs.fsType          = 0;
    fattrs.fsFontUse       = FATTR_FONTUSE_OUTLINE;
    strcpy(fattrs.szFacename,TNG_FONT);

    cFonts = GpiQueryFonts(hps, QF_PUBLIC,fattrs.szFacename, &lTemp,
             (LONG) sizeof(FONTMETRICS), (PFONTMETRICS)NULL);
    if (cFonts==0)
    {
        strcpy(fattrs.szFacename,"Helvetica");
    }

    GpiCreateLogFont(hps, (PSTR8)NULL, fontId, &fattrs);
    GpiSetCharSet(hps,fontId);

    sizfCharBox.cx=MAKEFIXED(14,0);
    sizfCharBox.cy=MAKEFIXED(20,0); 
    GpiSetCharBox(hps,&sizfCharBox);
    GpiSetTextAlignment(hps,TA_CENTER,TA_TOP);
}


void drawAxis(HPS hps,long yAxis12Dx,long yAxis12Dy,long yAxis22Dx,long yAxis22Dy,long xAxis12Dx,long xAxis12Dy,long xAxis22Dx,long xAxis22Dy,long zAxis12Dx,long zAxis12Dy,long zAxis22Dx,long zAxis22Dy,int drawIt )
{
    POINTL p;

    if (drawIt )
        GpiSetColor (hps,CLR_GREEN );
    else
        GpiSetColor (hps,CLR_BLACK );

    p.x=yAxis12Dx;
    p.y=yAxis12Dy;
    GpiMove(hps,&p);

    p.x=yAxis22Dx;
    p.y=yAxis22Dy;
    GpiLine(hps,&p);

    p.x=xAxis12Dx;
    p.y=xAxis12Dy;
    GpiMove(hps,&p);

    p.x=xAxis22Dx;
    p.y=xAxis22Dy;
    GpiLine(hps,&p);

    p.x=zAxis12Dx;
    p.y=zAxis12Dy;
    GpiMove(hps,&p);

    p.x=zAxis22Dx;
    p.y=zAxis22Dy;
    GpiLine(hps,&p);

}


void drawArrows(HPS hps,long yAxis22Dx,long yAxis22Dy,long yAr12Dx,long yAr12Dy,long yAr22Dx,long yAr22Dy,int drawIt )
{
    POINTL p;

    if (drawIt )
        GpiSetColor (hps,CLR_GREEN );
    else
        GpiSetColor (hps,CLR_BLACK );

    p.x=yAr12Dx;
    p.y=yAr12Dy;
    GpiMove(hps,&p);

    p.x=yAxis22Dx;
    p.y=yAxis22Dy;
    GpiLine(hps,&p);

    p.x=yAr22Dx;
    p.y=yAr22Dy;
    GpiLine(hps,&p);
}

void drawGrid(HPS hps,long gridX,long gridY,int drawIt,char *str,BOOL tracename)
{
    POINTL p;

    if (!tracename)
        return;

    if (drawIt )
        GpiSetColor (hps,CLR_CYAN );
    else
        GpiSetColor (hps,CLR_BLACK );

    p.x=gridX-10;
    p.y=gridY-10;
    GpiMove(hps,&p);

    p.x=gridX-13;
    p.y=gridY-9;
    GpiLine(hps,&p);

    p.x=gridX-13;
    p.y=gridY+9;
    GpiLine(hps,&p);

    p.x=gridX-10;
    p.y=gridY+10;
    GpiLine(hps,&p);

    p.x=gridX+10;
    p.y=gridY-10;
    GpiMove(hps,&p);

    p.x=gridX+13;
    p.y=gridY-9;
    GpiLine(hps,&p);

    p.x=gridX+13;
    p.y=gridY+9;
    GpiLine(hps,&p);

    p.x=gridX+10;
    p.y=gridY+10;
    GpiLine(hps,&p);

    p.x=gridX;
    p.y=gridY-15;

    if (drawIt )
        GpiSetColor (hps,CLR_WHITE );
    
    GpiCharStringAt(hps,&p,strlen(str),str);

}

int isInside(long x,long y)
{
   return ((x>0) && (x<screenSizeX) &&
           (y>0) && (y<screenSizeY));
}



void convertTo2D(float realX,float realY,float realZ,long *x,long *y)
{
    float q;

    if (realZ<=-4000 )
        realZ=-4000;

    q=1000.0/(realZ+5000.0f);

    *x=realX*q+screenSizeX/2;
    *y=realY*q+screenSizeY/2;
}

void turn2D(float *realX,float *realY,float sinA,float cosA)
{
     float dummy;

     dummy=*realX*cosA+*realY*sinA;
     *realY=*realY*cosA-*realX*sinA;
     *realX=dummy;
}


#define  AXISMY  -700
#define  AXISL    800
#define  AXISL2   700  
#define  ARROWL   100
#define  ARROWW    30


void	draw_thread(void *args)
{
   static long          b,b2,index;
	static int           distance;
   static RECTL         ScreenSize;
   static long          Color;
   static DATETIME	   DateTime;
   static POINTL        p1,p2,op1,op2;
   static POINTL        ptl[3];
   static long          width,height;
   static long          starcount,whiteStars,redStars,blueStars,yellowStars;
   static POINTL        *whitesegs,*redsegs,*bluesegs,*yellowsegs;  
   static POINTL        *whitesegs_o,*redsegs_o,*bluesegs_o,*yellowsegs_o;  
   static long          x[500],y[500];
   static float         realX[500],realY[500],realZ[500],sx[500],sy[500],sz[500],size[500];
   static float         ss,dfx,dfy,dfz,dd,f;
   static long          whitecount,redcount,bluecount,yellowcount;
   static long          whitecount_o,redcount_o,bluecount_o,yellowcount_o;
   static long          wc,rc,bc,yc;
   static long          gravity;
   static float         middleX,middleY,middleZ;

   int first;


   POINTL whitesegsAr[200]; 
   POINTL redsegsAr[200];
   POINTL bluesegsAr[200];
   POINTL yellowsegsAr[200];
   POINTL whitesegsAr_o[200]; 
   POINTL redsegsAr_o[200];
   POINTL bluesegsAr_o[200];
   POINTL yellowsegsAr_o[200];

   long gridX,gridY;
   long gridX_o,gridY_o;
   int isGrid;

   int inside;

   long searchNearX;
   long searchNearY;

   float dsx;
   float dsy;
   float ds;
   float minds;

   char starName[MAX_NAMESIZE];
   char starName_o[MAX_NAMESIZE];

   BOOL tracename;

   BOOL trid;

   float sinA;
   float cosA;

   float yAxis1Mx,yAxis1My,yAxis1Mz;
   float yAxis2Mx,yAxis2My,yAxis2Mz;
   float xAxis1Mx,xAxis1My,xAxis1Mz;
   float xAxis2Mx,xAxis2My,xAxis2Mz;
   float zAxis1Mx,zAxis1My,zAxis1Mz;
   float zAxis2Mx,zAxis2My,zAxis2Mz;

   float yAr1x,yAr1y,yAr1z;
   float yAr2x,yAr2y,yAr2z;
   float xAr1x,xAr1y,xAr1z;
   float xAr2x,xAr2y,xAr2z;
   float zAr1x,zAr1y,zAr1z;
   float zAr2x,zAr2y,zAr2z;


   long yAxis12Dx,yAxis12Dy;
   long yAxis22Dx,yAxis22Dy;
   long xAxis12Dx,xAxis12Dy;
   long xAxis22Dx,xAxis22Dy;
   long zAxis12Dx,zAxis12Dy;
   long zAxis22Dx,zAxis22Dy;

   long yAr12Dx,yAr12Dy;
   long yAr22Dx,yAr22Dy;
   long xAr12Dx,xAr12Dy;
   long xAr22Dx,xAr22Dy;
   long zAr12Dx,zAr12Dy;
   long zAr22Dx,zAr22Dy;

   HAB	   drawingthread_hab = WinInitialize(0);
   HMQ	   drawingthread_hmq = WinCreateMsgQueue(drawingthread_hab, 0);

   /* Settings galaxy parameter. */

   setTNGfont(hps); /* Setzen der Schriftart */


   whiteStars=configuration_data.whitestar_count;
   redStars=configuration_data.redstar_count;
   blueStars=configuration_data.bluestar_count;
   yellowStars=configuration_data.yellowstar_count;

   tracename=configuration_data.tracename;

   gravity=configuration_data.gravity;

   starcount=whiteStars+redStars+blueStars+yellowStars;

   width    = screenSizeX;
   height   = screenSizeY;

   trid=configuration_data.show3d;
   sinA=sin(-0.03f);
   cosA=cos(-0.03f);


   if ( trid )
   {
       yAxis1Mx=   0;
       yAxis1My=AXISMY;
       yAxis1Mz=   0;
       yAxis2Mx=   0;
       yAxis2My=AXISMY+AXISL;
       yAxis2Mz=   0;

       yAr1x=   ARROWW;
       yAr1y=   AXISMY+AXISL-ARROWL;
       yAr1z=      0;
       yAr2x=   -ARROWW;
       yAr2y=   AXISMY+AXISL-ARROWL;
       yAr2z=      0;

       xAxis1Mx=-AXISL2;
       xAxis1My=AXISMY;
       xAxis1Mz=   0;
       xAxis2Mx=AXISL2;
       xAxis2My=AXISMY;
       xAxis2Mz=   0;

       xAr1x=   AXISL2-ARROWL;
       xAr1y=   AXISMY;
       xAr1z=   ARROWW;
       xAr2x=   AXISL2-ARROWL;
       xAr2y=   AXISMY;
       xAr2z=   -ARROWW;


       zAxis1Mx=   0;
       zAxis1My=AXISMY;
       zAxis1Mz=-AXISL2;
       zAxis2Mx=   0;
       zAxis2My=AXISMY;
       zAxis2Mz=AXISL2;

       zAr1x=   ARROWW;
       zAr1y=   AXISMY;
       zAr1z=   AXISL2-ARROWL;
       zAr2x=   -ARROWW;
       zAr2y=   AXISMY;
       zAr2z=   AXISL2-ARROWL;

       convertTo2D(yAxis1Mx,yAxis1My,yAxis1Mz,&yAxis12Dx,&yAxis12Dy);
       convertTo2D(yAxis2Mx,yAxis2My,yAxis2Mz,&yAxis22Dx,&yAxis22Dy);
   }

   /* random seed (needed for each thread) */
	srand(WinGetCurrentTime(hab));


   /* Clear the background. */
   WinQueryWindowRect( hwndSaver, &ScreenSize );
   WinFillRect( hps, &ScreenSize, CLR_BLACK );


   /* Allocate memory. */
/*
   whitesegs = (POINTL *) ( malloc ( sizeof ( POINTL ) * starcount * 4 ) );
   redsegs = (POINTL *) ( malloc ( sizeof ( POINTL ) * starcount * 4 ) );
   bluesegs = (POINTL *) ( malloc ( sizeof ( POINTL ) * starcount * 4 ) );
   yellowsegs = (POINTL *) ( malloc ( sizeof ( POINTL ) * starcount * 4 ) );
*/   

   whitesegs = (POINTL *) &whitesegsAr;
   redsegs =   (POINTL *) &redsegsAr;
   bluesegs =  (POINTL *) &bluesegsAr;
   yellowsegs =(POINTL *) &yellowsegsAr;

   whitesegs_o = (POINTL *) &whitesegsAr_o;
   redsegs_o =   (POINTL *) &redsegsAr_o;
   bluesegs_o =  (POINTL *) &bluesegsAr_o;
   yellowsegs_o =(POINTL *) &yellowsegsAr_o;


  
   /* Init stars */
   for (b = 0; b < starcount; b++)
   {

      if (trid)
      {
          realX[b] = (rand()%(4000))-2000;
          realY[b] = (rand()%(2000))-1000;
          realZ[b] = (rand()%(4000))-2000;
      }
      else
      {
          realX[b] = (rand()%(4000))-2000;
          realY[b] = (rand()%(4000))-2000;
          realZ[b] = 0;
      }

      size[b]=1;
      if ( rand()%(10)>7 )
      {
         size[b]=4;
      }

      sx[b]=0;
      sy[b]=0; 
      sz[b]=0;

      x[b]=-1;
      y[b]=-1;
   }
 
   first=1;

   searchNearX=screenSizeX/2;
   searchNearY=screenSizeY/2;

   while(!stop_draw_thread)
   {
      DosGetDateTime ( &DateTime );
      srand ( DateTime.hundredths * 320 );

      if (trid )
      {
         turn2D(&xAxis1Mx,&xAxis1Mz,sinA,cosA);
         turn2D(&xAxis2Mx,&xAxis2Mz,sinA,cosA);
         turn2D(&zAxis1Mx,&zAxis1Mz,sinA,cosA);
         turn2D(&zAxis2Mx,&zAxis2Mz,sinA,cosA);

         turn2D(&yAr1x,&yAr1z,sinA,cosA);
         turn2D(&yAr2x,&yAr2z,sinA,cosA);

         turn2D(&xAr1x,&xAr1z,sinA,cosA);
         turn2D(&xAr2x,&xAr2z,sinA,cosA);

         turn2D(&zAr1x,&zAr1z,sinA,cosA);
         turn2D(&zAr2x,&zAr2z,sinA,cosA);
      }

      if (!first)
      {
         memcpy(whitesegs_o,whitesegs,sizeof ( POINTL ) * whitecount * 4);
         memcpy(redsegs_o,redsegs,sizeof ( POINTL ) * redcount * 4);
         memcpy(bluesegs_o,bluesegs,sizeof ( POINTL ) * bluecount * 4);
         memcpy(yellowsegs_o,yellowsegs,sizeof ( POINTL ) * yellowcount * 4);

         whitecount_o= whitecount;
         redcount_o=   redcount;
         bluecount_o=  bluecount;
         yellowcount_o=yellowcount;

         if (isGrid)
         {
            gridX_o=gridX;
            gridY_o=gridY;
            strcpy(starName_o,starName);
         }
      }
     
      
      /* <=- Stars -=> */

      whitecount=0;
      redcount=0;
      bluecount=0;
      yellowcount=0;

      wc=0;
      rc=0;
      bc=0;
      yc=0;


      middleX=0;
      middleY=0;
      middleZ=0;

      for (b = 0; b < starcount; b++)
      {
          if (trid )
          {
             turn2D(&realX[b],&realZ[b],sinA,cosA);
          }

          middleX+=realX[b];
          middleY+=realY[b];
          middleZ+=realZ[b];
      }
      middleX=middleX/starcount;
      middleY=middleY/starcount;
      middleZ=middleZ/starcount;
      for (b = 0; b < starcount; b++)
      {
          realX[b]-=middleX;
          if (!trid)
              realY[b]-=middleY;
          realZ[b]-=middleZ;
      }

      isGrid=0;

      minds=1e9;


      for (b = 0; b < starcount; b++)
      {
         for (b2=b+1 ;b2<starcount ;b2++)
         {
              if (trid)
              {
                  dfx=realX[b]-realX[b2];
                  dfy=realY[b]-realY[b2];
                  dfz=realZ[b]-realZ[b2];

                  dd=gravity*size[b2]/((dfx*dfx)+(dfy*dfy)+(dfz*dfz));           
                  dfx=dfx*dd;
                  dfy=dfy*dd;
                  dfz=dfz*dd;
                  sx[b]=sx[b]-dfx;
                  sy[b]=sy[b]-dfy;
                  sz[b]=sz[b]-dfz;

                  sx[b2]=sx[b2]+dfx;
                  sy[b2]=sy[b2]+dfy;
                  sz[b2]=sz[b2]+dfz;
              }
              else {
                  dfx=realX[b]-realX[b2];
                  dfy=realY[b]-realY[b2];

                  dd=gravity*size[b2]/((dfx*dfx)+(dfy*dfy));           
                  dfx=dfx*dd;
                  dfy=dfy*dd;
                  sx[b]=sx[b]-dfx;
                  sy[b]=sy[b]-dfy;

                  sx[b2]=sx[b2]+dfx;
                  sy[b2]=sy[b2]+dfy;
              }

         }
         realX[b]=realX[b]+sx[b];
         realY[b]=realY[b]+sy[b];
         realZ[b]=realZ[b]+sz[b];

         convertTo2D(realX[b],realY[b],realZ[b],&x[b],&y[b]);

         inside=isInside(x[b],y[b]);

         if ( wc<whiteStars )
         {
            wc++;
            if (inside)
            {
                index = whitecount*4;
                whitesegs[index].x = x[b];
                whitesegs[index].y = y[b];
                whitesegs[index+1].x = x[b]+2;
                whitesegs[index+1].y = y[b];
                whitesegs[index+2].x = x[b];
                whitesegs[index+2].y = y[b]+1;
                whitesegs[index+3].x = x[b]+2;
                whitesegs[index+3].y = y[b]+1;
                whitecount++;
            }
         }
         else {
            if (rc<redStars )
            {
                rc++;
                if (inside)
                {
                   index = redcount*4;
                   redsegs[index].x = x[b];
                   redsegs[index].y = y[b];
                   redsegs[index+1].x = x[b]+2;
                   redsegs[index+1].y = y[b];
                   redsegs[index+2].x = x[b];
                   redsegs[index+2].y = y[b]+1;
                   redsegs[index+3].x = x[b]+2;
                   redsegs[index+3].y = y[b]+1;
                   redcount++;
                }
            }
            else {
               if (bc<blueStars )
               {
                    bc++;
                    if (inside)
                    {
                       index = bluecount*4;
                       bluesegs[index].x = x[b];
                       bluesegs[index].y = y[b];
                       bluesegs[index+1].x = x[b]+2;
                       bluesegs[index+1].y = y[b];
                       bluesegs[index+2].x = x[b];
                       bluesegs[index+2].y = y[b]+1;
                       bluesegs[index+3].x = x[b]+2;
                       bluesegs[index+3].y = y[b]+1;
                       bluecount++;
                    }
               }
               else {
                    yc++;
                    if (inside)
                    {
                       index = yellowcount*4;
                       yellowsegs[index].x = x[b];
                       yellowsegs[index].y = y[b];
                       yellowsegs[index+1].x = x[b]+2;
                       yellowsegs[index+1].y = y[b];
                       yellowsegs[index+2].x = x[b];
                       yellowsegs[index+2].y = y[b]+1;
                       yellowsegs[index+3].x = x[b]+2;
                       yellowsegs[index+3].y = y[b]+1;
                       yellowcount++;
                    }
               }
             }
         }

         if (inside)
         {
             isGrid=1;
             dsx=(searchNearX-x[b]);
             dsy=(searchNearY-y[b]);
             ds=dsx*dsx+dsy*dsy;
             if (ds<minds)
             {
                 minds=ds;
                 gridX=x[b];
                 gridY=y[b];

                 strcpy(starName,starNames[b % CNT_STARS]);
             }
         }
      }

      if (!first)
      {
           drawGrid(hps,gridX_o,gridY_o,0,starName_o,tracename);

           if ( trid )
           {
               drawAxis(hps,yAxis12Dx,yAxis12Dy,yAxis22Dx,yAxis22Dy,xAxis12Dx,xAxis12Dy,xAxis22Dx,xAxis22Dy,zAxis12Dx,zAxis12Dy,zAxis22Dx,zAxis22Dy,0);
               drawArrows(hps,yAxis22Dx,yAxis22Dy,yAr12Dx,yAr12Dy,yAr22Dx,yAr22Dy,0);
               drawArrows(hps,xAxis22Dx,xAxis22Dy,xAr12Dx,xAr12Dy,xAr22Dx,xAr22Dy,0);
               drawArrows(hps,zAxis22Dx,zAxis22Dy,zAr12Dx,zAr12Dy,zAr22Dx,zAr22Dy,0);

           }

      }

      if (!first)
      {

          Color=CLR_BLACK;
          GpiSetColor ( hps , Color );
          GpiPolyLineDisjoint ( hps ,whitecount_o * 4 , whitesegs_o );
      }
      Color=CLR_WHITE;
      GpiSetColor ( hps , Color );
      GpiPolyLineDisjoint ( hps ,whitecount * 4 , whitesegs );

      if (!first)
      {
          Color=CLR_BLACK;
          GpiSetColor ( hps , Color );
          GpiPolyLineDisjoint ( hps ,redcount_o * 4 , redsegs_o );
      }
      Color=CLR_RED;
      GpiSetColor ( hps , Color );
      GpiPolyLineDisjoint ( hps ,redcount * 4 , redsegs );

      if (!first)
      {
          Color=CLR_BLACK;
          GpiSetColor ( hps , Color );
          GpiPolyLineDisjoint ( hps ,bluecount_o * 4 , bluesegs_o );
      }
      Color=CLR_BLUE;
      GpiSetColor ( hps , Color );
      GpiPolyLineDisjoint ( hps ,bluecount * 4 , bluesegs );

      if (!first)
      {
          Color=CLR_BLACK;
          GpiSetColor ( hps , Color );
          GpiPolyLineDisjoint ( hps ,yellowcount_o * 4 , yellowsegs_o );
      }
      Color=CLR_YELLOW;
      GpiSetColor ( hps , Color );
      GpiPolyLineDisjoint ( hps ,yellowcount * 4 , yellowsegs );

      if (isGrid)
      {
          drawGrid(hps,gridX,gridY,1,starName,tracename);
          searchNearX=gridX+(rand()%(4))-2;
          searchNearY=gridY+(rand()%(4))-2;
      }
      else {
           for (b = 0; b < starcount; b++)
           {
                if (trid)
                {
                    realX[b] = (rand()%(4000))-2000;
                    realY[b] = (rand()%(2000))-1000;
                    realZ[b] = (rand()%(4000))-2000;
                }
                else
                {
                    realX[b] = (rand()%(4000))-2000;
                    realY[b] = (rand()%(4000))-2000;
                    realZ[b] = 0;
                }
           }
      }

      if ( trid )
      {

         convertTo2D(xAxis1Mx,xAxis1My,xAxis1Mz,&xAxis12Dx,&xAxis12Dy);
         convertTo2D(xAxis2Mx,xAxis2My,xAxis2Mz,&xAxis22Dx,&xAxis22Dy);

         convertTo2D(zAxis1Mx,zAxis1My,zAxis1Mz,&zAxis12Dx,&zAxis12Dy);
         convertTo2D(zAxis2Mx,zAxis2My,zAxis2Mz,&zAxis22Dx,&zAxis22Dy);

         convertTo2D(yAr1x,yAr1y,yAr1z,&yAr12Dx,&yAr12Dy);
         convertTo2D(yAr2x,yAr2y,yAr2z,&yAr22Dx,&yAr22Dy);

         convertTo2D(xAr1x,xAr1y,xAr1z,&xAr12Dx,&xAr12Dy);
         convertTo2D(xAr2x,xAr2y,xAr2z,&xAr22Dx,&xAr22Dy);

         convertTo2D(zAr1x,zAr1y,zAr1z,&zAr12Dx,&zAr12Dy);
         convertTo2D(zAr2x,zAr2y,zAr2z,&zAr22Dx,&zAr22Dy);


         drawAxis(hps,yAxis12Dx,yAxis12Dy,yAxis22Dx,yAxis22Dy,xAxis12Dx,xAxis12Dy,xAxis22Dx,xAxis22Dy,zAxis12Dx,zAxis12Dy,zAxis22Dx,zAxis22Dy,1);

         drawArrows(hps,yAxis22Dx,yAxis22Dy,yAr12Dx,yAr12Dy,yAr22Dx,yAr22Dy,1);
         drawArrows(hps,xAxis22Dx,xAxis22Dy,xAr12Dx,xAr12Dy,xAr22Dx,xAr22Dy,1);
         drawArrows(hps,zAxis22Dx,zAxis22Dy,zAr12Dx,zAr12Dy,zAr22Dx,zAr22Dy,1);
      }


      first=0;
     
      /* sleep a while if necessary */
	   if(low_priority == TRUE)
          DosSleep(10);
      else
          DosSleep(2);
      }
/*   
   free ( whitesegs );
   free ( redsegs );
*/
   WinDestroyMsgQueue(drawingthread_hmq);
   WinTerminate(drawingthread_hab);

   _endthread();
}
