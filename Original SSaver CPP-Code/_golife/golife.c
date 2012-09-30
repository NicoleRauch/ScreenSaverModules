/*
	life.c
	life saver module C source file for SSaver version 2.1
	(C) 1994 Nicole Greiber
*/

#define INCL_DOS
#define INCL_DOSPROCESS
#define INCL_DOSMEMMGR
#define INCL_DOSERRORS
#define INCL_WIN
#define INCL_GPI
#include <os2.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <process.h>
#include <time.h>    /* time_t in GPFHandler */

#include "golife.h"


/* ===== preprocessor definitions */

#define MODULEVERSION		0x00020001
#define STACKSIZE		(64*1024)
#define ZORDERTIMERSTEP 	30000L
#define FUNCTION_SETINIFILENAME 7
#define WMP_MODULECRASH 	WM_USER + 13
#define SAVER_NAME_MAXLEN	32
#define FUNCTION_CONFIGURE	1
#define FUNCTION_STARTSAVER	2
#define FUNCTION_STOPSAVER	3
#define FUNCTION_QUERYNAME	4
#define FUNCTION_QUERYENABLED	5
#define FUNCTION_SETENABLED	6
#define CONFIGURATION_DEFAULT_ANIMATIONSPEED 4	/* 0=slow .. 4=fast */
/*
	$$$$$ insert code here $$$$$
	This is the place for your preprocessor definitions.

	$$$$$ for example $$$$$
*/
#define CONFIGURATION_MINIMUM_NUMBER 70
#define CONFIGURATION_DEFAULT_NUMBER 120
#define CONFIGURATION_MAXIMUM_NUMBER 250

/* ===== prototypes */

void	EXPENTRY SAVER_PROC(int function, HAB _hab, HWND hwndOwner, char *appname, void *buffer);
static	MRESULT EXPENTRY SaverWindowProc(HWND hwnd, ULONG msg, MPARAM mp1, MPARAM mp2);
static	MRESULT EXPENTRY ConfigureDlgProc(HWND hwnd, ULONG msg, MPARAM mp1, MPARAM mp2);
static	void	draw_thread(void *args);
static	void	priority_thread(void *args);
static	void	load_configuration_data(void);

static	void	write_profile_data(void);
static	ULONG	query_profile_data(void);
ULONG	GPFHandler(PEXCEPTIONREPORTRECORD pxcptrec, PEXCEPTIONREGISTRATIONRECORD prr, PCONTEXTRECORD pcr, PVOID pv);
void	print_contextrecord(FILE *logfile, PCONTEXTRECORD pcr);


/* ===== global data */

ULONG	MODULESOURCE_VERSION = 0x00020001;	/* EXPORTED: ScreenSaver 2.1/Pro 1.0 module source code (do not change) */

static	LONG	screenSizeX = 0;		/* screen size x */
static	LONG	screenSizeY = 0;		/* screen size y */
static	HWND	hwndSaver = NULLHANDLE; 	/* saver window handle */
static	HMODULE hmodDLL = NULLHANDLE;		/* saver module dll handle */
static	char	*application_name;		/* name of ScreenSaver app */
static	TID	tidDraw;			/* drawing-thread ID */
static	TID	tidPriority;			/* priority regulation thread ID */
static	HPS	hps;				/* presentation space handle */
static	HAB	hab;				/* anchor block handle */
static	BOOL	low_priority = TRUE;		/* low-priority flag */
static	BOOL	configuration_data_loaded = FALSE; /* config data loaded flag */
static	volatile BOOL stop_draw_thread; 	/* stop flag */
static	char	modulename[SAVER_NAME_MAXLEN+1]; /* module name buffer */
static	volatile BOOL stop_priority_thread = FALSE;

static	HWND	hwndApplication = NULLHANDLE;	/* window handle if ScreenSaver options dialog (main window) */
static	HDC	hdc;				/* device context handle */
static	char	ini_filename[CCHMAXPATH] = "";  /* name of INI file if other than OS2.INI */

static	struct	_configuration_data {
	ULONG	version;
	char	modulename[SAVER_NAME_MAXLEN+1];
	BOOL	enabled;
	int	animation_speed;
/*
	$$$$$ insert code here $$$$$
	If your saver module needs some additional configuration data,
	insert it here. It is automatically loaded and saved.

	$$$$$ for example $$$$$
*/
	int	number;
	BOOL gleiter;
} configuration_data;


/* ===== code */

/*
	GPFHandler - general protection fault exception handler
	There should be no reason to alter the code.
*/

ULONG	GPFHandler(PEXCEPTIONREPORTRECORD pxcptrec, PEXCEPTIONREGISTRATIONRECORD prr, PCONTEXTRECORD pcr, PVOID pv)
{
	if(pxcptrec->ExceptionNum == XCPT_ACCESS_VIOLATION){
		time_t	t;
		FILE	*logfile = fopen("crash.log", "a");
		t = time(NULL);
		fprintf(logfile, "Module %s crashed at %s", modulename, ctime(&t));
		fprintf(logfile, "ACCESS VIOLATION (xcpt # 0x%X)\n", (unsigned int)pxcptrec->ExceptionNum);
		fprintf(logfile, "xcpt info[0] flags = 0x%02X\n", (unsigned int)pxcptrec->ExceptionInfo[0]);
		fprintf(logfile, "xcpt info[1] faultaddr = 0x%08X\n", (unsigned int)pxcptrec->ExceptionInfo[1]);
		print_contextrecord(logfile, pcr);
		fclose(logfile);
		WinPostMsg(hwndApplication, WMP_MODULECRASH, MPVOID, MPVOID);
		_endthread();
/*		  DosExit(EXIT_THREAD, 1); */
	}
	return(XCPT_CONTINUE_SEARCH);
}

/*
	print_contextrecord - helper function for GPFHandler
	There should be no reason to alter the code.
*/
void	print_contextrecord(FILE *logfile, PCONTEXTRECORD pcr)
{
	fprintf(logfile, "  ContextFlags = 0x%08lX\n", pcr->ContextFlags);
	if(pcr->ContextFlags & CONTEXT_SEGMENTS){
		fprintf(logfile, "  GS = 0x%08lX\n", pcr->ctx_SegGs);
		fprintf(logfile, "  FS = 0x%08lX\n", pcr->ctx_SegFs);
		fprintf(logfile, "  ES = 0x%08lX\n", pcr->ctx_SegEs);
		fprintf(logfile, "  DS = 0x%08lX\n", pcr->ctx_SegDs);
	}
	if(pcr->ContextFlags & CONTEXT_INTEGER){
		fprintf(logfile, "  EAX = 0x%08lX\n", pcr->ctx_RegEax);
		fprintf(logfile, "  EBX = 0x%08lX\n", pcr->ctx_RegEbx);
		fprintf(logfile, "  ECX = 0x%08lX\n", pcr->ctx_RegEcx);
		fprintf(logfile, "  EDX = 0x%08lX\n", pcr->ctx_RegEdx);
		fprintf(logfile, "  EDI = 0x%08lX\n", pcr->ctx_RegEdi);
		fprintf(logfile, "  ESI = 0x%08lX\n", pcr->ctx_RegEsi);
	}
	if(pcr->ContextFlags & CONTEXT_CONTROL){
		fprintf(logfile, "  SS = 0x%08lX\n", pcr->ctx_SegSs);
		fprintf(logfile, "  ESP = 0x%08lX\n", pcr->ctx_RegEsp);
		fprintf(logfile, "  EBP = 0x%08lX\n", pcr->ctx_RegEbp);
		fprintf(logfile, "  EFLAGS = 0x%08lX\n", pcr->ctx_EFlags);
		fprintf(logfile, "  CS = 0x%08lX\n", pcr->ctx_SegCs);
		fprintf(logfile, "  EIP = 0x%08lX\n", pcr->ctx_RegEip);
	}
}

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
	hab = _hab;
	application_name = appname;
	hwndApplication = hwndOwner;
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
	static	HWND	hwndSpeed;
/*
	$$$$$ insert code here $$$$$
	If you need additional data for dialog processing, insert it here.

	$$$$$ for example $$$$$
*/
	static HWND hwndNumber;
	CHAR msgtext[256];

	switch(msg){
	case WM_INITDLG:
	    /* set titlebar of the dialog window */
	    /* to "MODULENAME configuration" */
		strcpy(buf, modulename);
		strcat(buf, " configuration");
		WinSetWindowText(hwnd, (PSZ)buf);

		/* get window handles of the dialog controls */
		/* and set initial state of the controls */
		hwndEnabled = WinWindowFromID(hwnd, IDC_ENABLED);
		WinSendMsg(hwndEnabled, BM_SETCHECK,
		  MPFROMSHORT(configuration_data.enabled), MPVOID);
		hwndSpeed = WinWindowFromID(hwnd, IDC_SPEED);
		WinSendMsg(hwndSpeed, SLM_SETTICKSIZE, MPFROM2SHORT(SMA_SETALLTICKS, 3), MPVOID);
		WinSendMsg(hwndSpeed, SLM_SETSLIDERINFO, MPFROM2SHORT(SMA_SLIDERARMPOSITION, SMA_INCREMENTVALUE), MPFROMLONG(configuration_data.animation_speed));
		WinSendMsg(hwndSpeed, SLM_SETSCALETEXT, MPFROMSHORT(0), MPFROMP("slow"));
		WinSendMsg(hwndSpeed, SLM_SETSCALETEXT, MPFROMSHORT(4), MPFROMP("fast"));
/*
		$$$$$ insert code here $$$$$
		Get window handles of your dialog controls.
		Set initial state of your controls.

		$$$$$ for example $$$$$
*/
		hwndNumber = WinWindowFromID(hwnd, IDC_NUMBER);
		WinSendMsg(hwndNumber, SPBM_SETLIMITS, (MPARAM)CONFIGURATION_MAXIMUM_NUMBER, (MPARAM)CONFIGURATION_MINIMUM_NUMBER);
		WinSendMsg(hwndNumber, SPBM_SETCURRENTVALUE, MPFROMLONG(configuration_data.number), MPVOID);

		(VOID)WinCheckButton( hwnd, (USHORT)IDC_RANDOM, !configuration_data.gleiter );
		(VOID)WinCheckButton( hwnd, (USHORT)IDC_GLEITER, configuration_data.gleiter );
		/* return FALSE since we did not change the focus */
		return (MRESULT)FALSE;
	case WM_COMMAND:
		switch(SHORT1FROMMP(mp1)){
		case IDC_OK:
		    /* OK button was pressed. query the control settings */
			configuration_data.enabled = SHORT1FROMMR(WinSendMsg(hwndEnabled, BM_QUERYCHECK, MPVOID, MPVOID));
			configuration_data.animation_speed = SHORT1FROMMR(WinSendMsg(hwndSpeed, SLM_QUERYSLIDERINFO, MPFROM2SHORT(SMA_SLIDERARMPOSITION, SMA_INCREMENTVALUE), MPVOID));
/*
			$$$$$ insert code here $$$$$
			Query control settings of your controls.

			$$$$$ for example $$$$$
*/
			WinSendMsg(hwndNumber, SPBM_QUERYVALUE, MPFROMP(&configuration_data.number), MPFROM2SHORT(0, SPBQ_DONOTUPDATE));
			/* check if the number is in the allowed range	*/
			if( CONFIGURATION_MINIMUM_NUMBER > configuration_data.number ||
				 configuration_data.number > CONFIGURATION_MAXIMUM_NUMBER ) {
				 	sprintf( msgtext, "Sorry, but the number of cells in the horizontal direction must be between %d and %d",
				 				CONFIGURATION_MINIMUM_NUMBER, CONFIGURATION_MAXIMUM_NUMBER );
				 	WinMessageBox( HWND_DESKTOP, hwnd, (PSZ)msgtext, (PSZ)"Life",
										0, MB_OK | MB_INFORMATION | MB_MOVEABLE );
			} else { 	/* everything is o.k.	*/			
				if( WinQueryButtonCheckstate( hwnd, IDC_RANDOM ) == 1 )
					configuration_data.gleiter = FALSE;
				else
					configuration_data.gleiter = TRUE;	
				/* write all configuration data to INI-file */
				write_profile_data();
				/* end dialog */
				WinDismissDlg(hwnd, TRUE);
			}
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
	static	SIZEL	pagesize={0, 0};
	switch(msg){
	case WM_CREATE:
	    /* reset the "stop" flag */
		stop_draw_thread = FALSE;
		/* store window handle */
		hwndSaver = hwnd;
		/* get hdc and create normal presentation space */
		hdc = WinOpenWindowDC (hwnd);
		hps = GpiCreatePS (hab, hdc, &pagesize, GPIA_ASSOC|PU_PELS|GPIT_NORMAL);
		/* start the drawing-thread */
		tidDraw = _beginthread(draw_thread, NULL, STACKSIZE, NULL);
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
	    /* destroy the presentation space */
		GpiDestroyPS(hps);
		/* stop the z-order timer */
		WinStopTimer(hab, hwndSaver, IDT_ZORDERTIMER);
		break;
	case WM_PAINT:
	    /* just validate the update area. all drawing is done */
	    /* in the drawing-thread. */
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
		    /* resource string not found. indicate error by */
		    /* setting module name to empty string (the name */
		    /* "" is interpreted as an error by SSDLL.DLL and */
		    /* the module does not show up in the list box). */
			strcpy(modulename, "");
			return;
		}
		/* load data from INI-file. the key name is the name of the */
		/* saver module */
		size = query_profile_data();
		if(size != sizeof(configuration_data)
		  || configuration_data.version != MODULEVERSION
		  || strncmp(modulename, configuration_data.modulename, sizeof(modulename)) != 0){
		    /* if entry is not found or entry has invalid size or */
		    /* entry has wrong version number, create a new entry */
		    /* with default values and write it to the INI-file */
			configuration_data.version = MODULEVERSION;
			configuration_data.enabled = TRUE;
			configuration_data.animation_speed = CONFIGURATION_DEFAULT_ANIMATIONSPEED;
			strncpy(configuration_data.modulename, modulename, sizeof(configuration_data.modulename));
/*
			$$$$$ insert code here $$$$$
			If you have added data to the configuration_data
			structure, insert code here to set the default
			values for your data items.

			$$$$$ for example $$$$$
*/
			configuration_data.number = CONFIGURATION_DEFAULT_NUMBER;
			configuration_data.gleiter = TRUE;
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
	    return; 	/* don't write if not OS2.INI */

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
This module displays the game of life. You can either select
a random population to start with, or a "Gleiterkanone".
The principle is that the screen is divided into cells. In the
"Module Configuration" window you can select how many cells will
be displayed in the horizontal direction. The more cells there
are, the slower the animation will be. Every cell has 8 neighbors,
like this:
            NNN
				NCN
				NNN

where C is the cell and N are the neighbors. When a cell has
two or three neighbors that are "alive" (displayed white), it will
also come to life or stay alive if it has been alive before. If
there are more or less living neighbors, it will die.
The "Gleiterkanone" (glider cannon) is a special kind of "animal":
It always returns to its original shape, and it produces little
gliders" that flap across the screen.

*/

inline VOID DrawCell (const HPS hps, const INT x, const INT y,
							const INT delta, const BYTE fCellState )
{
	RECTL rcl;

	rcl.xLeft = (x - 1) * delta;
	rcl.xRight = rcl.xLeft + delta - 1;
	rcl.yBottom = (y - 1) * delta;
	rcl.yTop = rcl.yBottom + delta - 1;

	WinFillRect (hps, &rcl, fCellState & 1 ? CLR_WHITE : CLR_BLACK);
}

inline unsigned char CheckSum( unsigned char sum )
{
	switch( sum ){
		case 3:
		case 11:
	case 12: return 1;	/* turn / keep field on */
	}
	return 0;	/* turn / keep field off */
}


INT BotLife( HPS hps, unsigned char **Fields,
				 INT xcells, INT ycells, INT delta )
    /* checks the field in the lower 4 bits, writes the new field */
    /* into the upper 4 bits */
{
	unsigned char *aktfield, *toprow, *botrow, **aktrow;
	INT xcount, ycount = 1;
	unsigned char sum;
	INT DrawSum = 0;

	aktrow = Fields;
	aktrow++;
	aktfield = *aktrow;
	aktfield++;
	toprow = *(aktrow + 1);
	toprow++;
	botrow = *(aktrow - 1);
	botrow++;

	for(ycount = 1; ycount <= ycells; ycount++, aktfield++, toprow++, botrow++)
	{
		for(xcount = 1; xcount <= xcells;
			 xcount++, aktfield++, toprow++, botrow++ )
		{
			sum = *(aktfield - 1) + *(aktfield + 1) +
					*(toprow - 1) + *toprow + *(toprow + 1) +
					*(botrow - 1) + *botrow + *(botrow + 1);
			sum = (sum | *aktfield) & 0x0F;
			*aktfield &= 15;
			*aktfield += ( ( sum == 3 ) << 4 );	/* write into the upper 4 bits */
			if( (*aktfield & 1) != (*aktfield >> 4) ){	/* state of the cell has changed */
				DrawCell(hps, xcount, ycount, delta, (*aktfield >> 4) );
				DrawSum++;
			}
				
		}
		botrow = *aktrow++;
		aktfield = *aktrow;
		toprow = *(aktrow + 1);
	}
	return DrawSum;
}


INT TopLife( HPS hps, unsigned char **Fields,
				 INT xcells, INT ycells, INT delta )
    /* checks the field in the upper 4 bits, writes the new field */
    /* into the lower 4 bits */
{
	unsigned char *aktfield, *toprow, *botrow, **aktrow;
	INT xcount, ycount;
	unsigned char sum;
	INT DrawSum = 0;

	aktrow = Fields;
	aktrow++;
	aktfield = *aktrow;
	aktfield++;
	toprow = *(aktrow + 1);
	toprow++;
	botrow = *(aktrow - 1);
	botrow++;

	for(ycount = 1; ycount <= ycells; ycount++, aktfield++, toprow++, botrow++)
	{
		for(xcount = 1; xcount <= xcells;
			 xcount++, aktfield++, toprow++, botrow++ )
		{
			sum = *(aktfield - 1) + *(aktfield + 1) +
					*(toprow - 1) + *toprow + *(toprow + 1) +
					*(botrow - 1) + *botrow + *(botrow + 1);
			sum = (sum | *aktfield) >> 4;
			*aktfield &= 0xF0;	/* erase the lower 4 bits */
			*aktfield += ( sum == 3 );	/* write into the lower 4 bits */
			if( (*aktfield & 1) != (*aktfield >> 4) ){
				DrawCell(hps, xcount, ycount, delta, (*aktfield & 1) );
				DrawSum++;
			}
				
		}
		botrow = *aktrow++;
		aktfield = *aktrow;
		toprow = *(aktrow + 1);
	}
	return DrawSum;
}





VOID init_gleiter( HPS hps, unsigned char **Fields,
						 INT xcells, INT ycells, INT delta )
{

	INT x, y;
	
	inline VOID turn_on( INT x, INT y )
	{
		Fields[y][x] = 1;
		DrawCell(hps, x, y, delta, 1 );
	}

	if( xcells * ycells < 10 ) return;
	x = xcells / 3 + 5;
	y = ycells / 6;
	turn_on( x, y );
	x -= 3;
	turn_on( x, ++y );
	turn_on(++x, y); turn_on(++x, y); turn_on(++x, y);
	x -= 12;
	turn_on( x, ++y);
	x += 8;
	turn_on(x, y);
	turn_on(++x, y); turn_on(++x, y); turn_on(++x, y);
	x += 10;
	turn_on( x, y);
	turn_on(++x, y);
	x -= 23;
	turn_on( x, ++y);
	x+= 2;
	turn_on( x, y);
	x += 7;
	turn_on(x, y); x += 3; turn_on( x, y);
	x += 10;
	turn_on(x, y); turn_on( ++x, y);
	x -= 35;
	turn_on(x, ++y);
	turn_on(++x, y); 
	x += 10; turn_on(x, y);
	x += 4; turn_on(x, y);	
	turn_on(++x, y);
	x += 5; turn_on(x, y);
	turn_on(++x, y); turn_on(++x, y); turn_on(++x, y);
	x += 6; turn_on(x, y);
	x -= 30;
	turn_on(x, ++y);
	turn_on(++x, y);
	x += 10; turn_on(x, y);
	x += 4; turn_on(x, y);	
	turn_on(++x, y);
	x += 6; turn_on(x, y);
	turn_on(++x, y); turn_on(++x, y); turn_on(++x, y);
	x += 5; turn_on( x, y);
	x -= 19;
	turn_on( x, ++y);
	x += 4; turn_on( x, y);	
	turn_on(++x, y);
	x += 9; turn_on(x, y);
	x -= 13;
	turn_on( x, ++y);
	x += 2; turn_on( x, y);
	turn_on( --x, ++y);
}

VOID init_random( HPS hps, unsigned char **Wanderer,
						INT xcells, INT ycells, INT delta, INT number )
{
	unsigned char **yrun = Wanderer;
	INT x = 1, y = 1;

	yrun++;

	do{
		*(*yrun + x) = 1;
		DrawCell(hps, x, y, delta, 1 );
		x += rand() % (13 * number) + 1;
		if( x > xcells ){
		    yrun++;		/* go to the next line */
			y++;
			x -= xcells;
		}
	} while ( y <= ycells );
}

void draw_thread(void *args)
{
    const RECTL ScreenRect = {0, 0, screenSizeX, screenSizeY };
    INT delta, DrawSum1 = 0, DrawSum2 = 0, OldDrawSum1 = 0, OldDrawSum2 = 0,
	OldDrawSum3 = 0,
	DrawNew1 = 0, DrawNew2 = 0, DrawNew3 = 0;
    INT xcells, ycells;
    unsigned char **Fields;
    INT i, j;
    EXCEPTIONREGISTRATIONRECORD	xcpthand = { (PEXCEPTIONREGISTRATIONRECORD)0, 
					     GPFHandler };
    DosSetExceptionHandler(&xcpthand) ;

    /* random seed (needed for each thread) */
    srand(WinGetCurrentTime(hab));

    /* calculate size of one field */
    delta = screenSizeX / configuration_data.number;
    xcells = screenSizeX / delta;
    ycells = screenSizeY / delta;

    if ((LONG) (xcells + 2) * (ycells + 2) > 67000L)
	{
	    DosBeep( 500, 150 );
	    xcells = ycells = 0;
	}
	
    /* allocate memory for Fields */
    Fields = (unsigned char**)malloc( (ycells + 2) * sizeof(unsigned char*) );

    if( !Fields ) goto end;
		
    for( i = 0; i < ycells + 2 && !stop_draw_thread; i++ ){
	Fields[i] = (unsigned char *)malloc( 
	    (xcells + 2) * sizeof(unsigned char) );
	if ( !Fields[i] ) goto end;
    }
    /* initialize Fields */
    for( i = 0; i < ycells + 2; i++)
	for( j = 0; j < xcells + 2; j++ )
	    Fields[i][j] = 0;


    WinFillRect( hps, &ScreenRect, CLR_BLACK );

    if( configuration_data.gleiter )
	init_gleiter( hps, Fields, xcells, ycells, delta );
    else
	init_random( hps, Fields, xcells, ycells, delta, 1 );

    i = 0;
    while(!stop_draw_thread){
	
	DrawSum1 = BotLife( hps, Fields, xcells, ycells, delta );
	if( !configuration_data.gleiter ){
	    if( DrawSum1 == DrawSum2 )
		DrawNew1++;
	    else
		DrawNew1 = 0;
	    if( OldDrawSum1 == DrawSum1 )
		DrawNew2++;
	    else
		DrawNew2 = 0;
	    OldDrawSum1 = DrawSum1;
	    if( i == 0 ){
		if( DrawSum1 == OldDrawSum3 )
		    DrawNew3++;
		else {
		    DrawNew3 = 0;
		    OldDrawSum3 = DrawSum1;
		}
	    }
	    i++;
	    i %= 3;
	}		/* end if !configuration_data.gleiter */
				

	/* sleep if necessary */
	if(low_priority == FALSE)
	    DosSleep(1);

	/* sleep if user requests slower animation */
	switch(configuration_data.animation_speed){
	case 4: break;
	case 3: DosSleep(10); break;
	case 2: DosSleep(30); break;
	case 1: DosSleep(50); break;
	case 0: DosSleep(70); break;
	}

	DrawSum2 = TopLife( hps, Fields, xcells, ycells, delta );

	if( !configuration_data.gleiter ){
	    if( DrawSum1 == DrawSum2 )
		DrawNew1++;
	    else
		DrawNew1 = 0;	

	    if( i == 0 ){
		if( DrawSum2 == OldDrawSum3 )
		    DrawNew3++;
		else {
		    DrawNew3 = 0;
		    OldDrawSum3 = DrawSum2;
		}
	    }
	    i++;
	    i %= 3;	
			
	    if( DrawNew1 > 100 || DrawNew3 > 30 || DrawNew2 > 50 ){
		init_random( hps, Fields, xcells, ycells, delta, xcells / 13 );
		DrawNew1 = 0;
		DrawNew2 = 0;
		DrawNew3 = 0;
	    }	/* if the screen is too static, do a new initialization	*/
	}		/* end if !configuration_data.gleiter */


	/* sleep if necessary */
	if(low_priority == FALSE)
	    DosSleep(1);

	/* sleep if user requests slower animation */
	switch(configuration_data.animation_speed){
	case 4: break;
	case 3: DosSleep(10); break;
	case 2: DosSleep(30); break;
	case 1: DosSleep(50); break;
	case 0: DosSleep(70); break;
	}
    }	/* while !stop_draw_thread */

    /* free resources */
  end:
    /* deallocate memory of Fields */
    for( i = 0; i < ycells + 2; i++ )
	    free( Fields[i] );
    free( Fields );

    DosUnsetExceptionHandler(&xcpthand);
    _endthread();
}
