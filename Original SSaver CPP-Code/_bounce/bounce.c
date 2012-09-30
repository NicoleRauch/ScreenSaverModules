/*
	bounce.c
	written by Nicole Greiber with the help of the
	sample saver module C source file version 1.2
	(C) 1993-94 Siegfried Hanisch
*/

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
#include <time.h>    // time_t in GPFHandler

#include "bounce.h"


// ===== preprocessor definitions

#define MODULEVERSION           0x00020001
#define STACKSIZE               (64*1024)
#define SAVER_NAME_MAXLEN	32
#define ZORDERTIMERSTEP         30000L
#define FUNCTION_CONFIGURE	1
#define FUNCTION_STARTSAVER	2
#define FUNCTION_STOPSAVER	3
#define FUNCTION_QUERYNAME	4
#define FUNCTION_QUERYENABLED	5
#define FUNCTION_SETENABLED	6
#define FUNCTION_SETINIFILENAME 7
#define WMP_MODULECRASH         WM_USER + 13
#define CONFIGURATION_DEFAULT_ANIMATIONSPEED 2	// 0=slow .. 4=fast
/*
	$$$$$ insert code here $$$$$
	This is the place for your preprocessor definitions.

	$$$$$ for example $$$$$
*/
#define CONFIGURATION_MINIMUM_COUNT 1
#define CONFIGURATION_DEFAULT_COUNT 32
#define CONFIGURATION_MAXIMUM_COUNT 100
#define COLORS_MINIMUM 1
#define COLORS_DEFAULT 10
#define COLORS_MAXIMUM 50
#define CONFIGURATION_DEFAULT_BLANKSCREEN FALSE	// do not blank screen

// ===== prototypes

void	EXPENTRY SAVER_PROC(int function, HAB _hab, HWND hwndOwner, char *appname, void *buffer);
static	MRESULT EXPENTRY SaverWindowProc(HWND hwnd, ULONG msg, MPARAM mp1, MPARAM mp2);
static	MRESULT EXPENTRY ConfigureDlgProc(HWND hwnd, ULONG msg, MPARAM mp1, MPARAM mp2);
static	void	draw_thread(void *args);
static	void	priority_thread(void *args);
static	void	load_configuration_data(void);
// new:
static  void    write_profile_data(void);
static  ULONG   query_profile_data(void);
ULONG   GPFHandler(PEXCEPTIONREPORTRECORD pxcptrec, PEXCEPTIONREGISTRATIONRECORD prr, PCONTEXTRECORD pcr, PVOID pv);
void    print_contextrecord(FILE *logfile, PCONTEXTRECORD pcr);


// ===== global data

ULONG   MODULESOURCE_VERSION = 0x00020001;      // EXPORTED: ScreenSaver 2.1/Pro 1.0 module source code (do not change)

static	LONG	screenSizeX = 0;		// screen size x
static	LONG	screenSizeY = 0;		// screen size y
static	HWND	hwndSaver = NULLHANDLE; 	// saver window handle
static	HMODULE hmodDLL = NULLHANDLE;		// saver module dll handle
static  HWND    hwndApplication = NULLHANDLE;   // window handle if ScreenSaver options dialog (main window)
static	char	*application_name;		// name of ScreenSaver app
static	TID	tidDraw;			// drawing-thread ID
static	TID	tidPriority;			// priority regulation thread ID
static  HDC     hdc;                            // device context handle
static	HPS	hps;				// presentation space handle
static	HAB	hab;				// anchor block handle
static	BOOL	low_priority = TRUE;		// low-priority flag
static	BOOL	configuration_data_loaded = FALSE; // config data loaded flag
static	volatile BOOL stop_draw_thread; 	// stop flag
static	char	modulename[SAVER_NAME_MAXLEN+1]; // module name buffer
static	volatile BOOL stop_priority_thread = FALSE;
static  char    ini_filename[CCHMAXPATH] = "";  // name of INI file if other than OS2.INI


static	struct	_configuration_data {
	ULONG	version;
        char    modulename[SAVER_NAME_MAXLEN+1];
	BOOL	enabled;
	int	animation_speed;
/*
	$$$$$ insert code here $$$$$
	If your saver module needs some additional configuration data,
	insert it here. It is automatically loaded and saved.

	$$$$$ for example $$$$$
*/
	int	count;
	int   colors;
	BOOL  blank_screen;
} configuration_data;

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
//		  DosExit(EXIT_THREAD, 1);
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

// ===== code

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
	// load all configuration data from INI-file
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
		// call the configuration dialog
		WinDlgBox(HWND_DESKTOP, hwndOwner, ConfigureDlgProc,
		  hmodDLL, IDD_CONFIGURE, application_name);
		return;
	case FUNCTION_STARTSAVER:
		// start the saver
		// get "low priority" state from supplied buffer (BOOL *)
		low_priority = *((BOOL *)buffer);
		// query size of screen
		screenSizeX = WinQuerySysValue(HWND_DESKTOP, SV_CXSCREEN);
		screenSizeY = WinQuerySysValue(HWND_DESKTOP, SV_CYSCREEN);
		// register window class for the saver window
		WinRegisterClass(hab, (PSZ)modulename,
		  (PFNWP)SaverWindowProc, 0, 0);
		// create the saver window
		hwndSaver = WinCreateWindow(HWND_DESKTOP, (PSZ)modulename,
		  (PSZ)NULL, WS_VISIBLE, 0, 0, screenSizeX, screenSizeY,
		  HWND_DESKTOP, HWND_TOP, 0, NULL, NULL);
		return;
	case FUNCTION_STOPSAVER:
		// stop the saver
		if(low_priority){
			stop_priority_thread = TRUE;
			DosSetPriority(PRTYS_THREAD, PRTYC_REGULAR, 0, tidPriority);
			DosSetPriority(PRTYS_THREAD, PRTYC_REGULAR, 0, tidDraw);
			DosWaitThread(&tidPriority, DCWW_WAIT);
//			  DosKillThread(tidPriority);	***** kills thread 1 under OS/2 2.11
		}
		// tell drawing-thread to stop
		stop_draw_thread = TRUE;
		if(DosWaitThread(&tidDraw, DCWW_NOWAIT) == ERROR_THREAD_NOT_TERMINATED){
			// if priority of drawing-thread was set to idle time
			// priority, set it back to normal value
			DosSetPriority(PRTYS_THREAD, PRTYC_REGULAR, PRTYD_MAXIMUM, tidDraw);
			// wait until drawing-thread has ended
			DosWaitThread(&tidDraw, DCWW_WAIT);
		}

		if(hwndSaver != NULLHANDLE){
			// move saver window to front
			WinSetWindowPos(hwndSaver, HWND_TOP,
			  0, 0, 0, 0, SWP_ZORDER);
			// destroy saver window
			WinDestroyWindow(hwndSaver);
			hwndSaver = NULLHANDLE;
		}
		return;
	case FUNCTION_QUERYNAME:
		// copy module name to supplied buffer (CHAR *)
		strcpy(buffer, modulename);
		return;
	case FUNCTION_QUERYENABLED:
		// copy "enabled" state to supplied buffer (BOOL *)
		*((BOOL *)buffer) = configuration_data.enabled;
		return;
	case FUNCTION_SETENABLED:
		// get new "enabled" state from supplied buffer (BOOL *)
		configuration_data.enabled = *((BOOL *)buffer);
		write_profile_data();
		return;
	}

	// illegal function request
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
	case 0: // initializing DLL
		hmodDLL = hmod;
		return 1;
	case 1: // terminating DLL
		return 1;
	default:
		// return error
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
    static	HWND	hwndCount;
    static HWND hwndColor;
    static HWND hwndBlankScreen;
    CHAR msgtext[256];

    switch(msg){
    case WM_INITDLG:
	// set titlebar of the dialog window
	// to "MODULENAME configuration"
	strcpy(buf, modulename);
	strcat(buf, " configuration");
	WinSetWindowText(hwnd, (PSZ)buf);

	// get window handles of the dialog controls
	       // and set initial state of the controls
	       hwndEnabled = WinWindowFromID(hwnd, IDC_ENABLED);
	WinSendMsg(hwndEnabled, BM_SETCHECK,
		   MPFROMLONG(configuration_data.enabled), MPVOID);
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
	hwndCount = WinWindowFromID(hwnd, IDC_COUNT);
	WinSendMsg(hwndCount, SPBM_SETLIMITS, (MPARAM)CONFIGURATION_MAXIMUM_COUNT, (MPARAM)CONFIGURATION_MINIMUM_COUNT);
	WinSendMsg(hwndCount, SPBM_SETCURRENTVALUE, MPFROMLONG(configuration_data.count), MPVOID);

/* set window that queries how many lines will have the same color	*/
	hwndColor = WinWindowFromID(hwnd, IDC_COLOR);
	WinSendMsg(hwndColor, SPBM_SETLIMITS, (MPARAM)COLORS_MAXIMUM, (MPARAM)COLORS_MINIMUM);
	WinSendMsg(hwndColor, SPBM_SETCURRENTVALUE, MPFROMLONG(configuration_data.colors), MPVOID);

	hwndBlankScreen = WinWindowFromID(hwnd, IDC_BLANKSCREEN);
	WinSendMsg(hwndBlankScreen, BM_SETCHECK,
		   MPFROMSHORT(configuration_data.blank_screen), MPVOID);

	// return FALSE since we did not change the focus
	       return (MRESULT)FALSE;
    case WM_COMMAND:
	switch(SHORT1FROMMP(mp1)){
	case IDC_OK:
	    // OK button was pressed. query the control settings
	    configuration_data.enabled = SHORT1FROMMR(WinSendMsg(hwndEnabled, BM_QUERYCHECK, MPVOID, MPVOID));
	    configuration_data.animation_speed = SHORT1FROMMR(WinSendMsg(hwndSpeed, SLM_QUERYSLIDERINFO, MPFROM2SHORT(SMA_SLIDERARMPOSITION, SMA_INCREMENTVALUE), MPVOID));
/*
  $$$$$ insert code here $$$$$
  Query control settings of your controls.

  $$$$$ for example $$$$$
  */
	    WinSendMsg(hwndCount, SPBM_QUERYVALUE, MPFROMP(&configuration_data.count), MPFROM2SHORT(0, SPBQ_DONOTUPDATE));
	    WinSendMsg(hwndColor, SPBM_QUERYVALUE, MPFROMP(&configuration_data.colors), MPFROM2SHORT(0, SPBQ_DONOTUPDATE));
	    configuration_data.blank_screen = SHORT1FROMMR(WinSendMsg(hwndBlankScreen, BM_QUERYCHECK, MPVOID, MPVOID));

/* check if the values are in the allowed range	*/
	    if(CONFIGURATION_MINIMUM_COUNT > configuration_data.count ||
	       configuration_data.count > CONFIGURATION_MAXIMUM_COUNT){
		sprintf( msgtext, "Sorry, but the number of lines displayed must be between %d and %d!",
			 CONFIGURATION_MINIMUM_COUNT, CONFIGURATION_MAXIMUM_COUNT);
		WinMessageBox( HWND_DESKTOP, hwnd, (PSZ)msgtext, (PSZ)"Bouncing Line",
			       0, MB_OK | MB_INFORMATION | MB_MOVEABLE );
	    } else {
		if( COLORS_MINIMUM > configuration_data.colors ||
		    configuration_data.colors > COLORS_MAXIMUM ) {
		    sprintf( msgtext, "Sorry, but the number of lines that are displayed in the same color must be between %d and %d!",
			     COLORS_MINIMUM, COLORS_MAXIMUM );
		    WinMessageBox( HWND_DESKTOP, hwnd, (PSZ)msgtext, (PSZ)"Bouncing Line",
				   0, MB_OK | MB_INFORMATION | MB_MOVEABLE );
		} else {	/* everything is o.k.	*/
		    /* write all configuration data to INI-file */
		    write_profile_data();
		    // end dialog
			   WinDismissDlg(hwnd, TRUE);
		}
	    }
	    return (MRESULT)0;
	case IDC_CANCEL:
	    // dialog was cancelled; end it
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
		// reset the "stop" flag
		stop_draw_thread = FALSE;
		// store window handle
		hwndSaver = hwnd;
		// get hdc and create normal presentation space
		hdc = WinOpenWindowDC (hwnd);
		hps = GpiCreatePS (hab, hdc, &pagesize, GPIA_ASSOC|PU_PELS|GPIT_NORMAL);
		// start the drawing-thread
		tidDraw = _beginthread(draw_thread, NULL, STACKSIZE, NULL);
		// create thread to control priority of drawing thread
		if(low_priority){
			stop_priority_thread = FALSE;
			DosCreateThread(&tidPriority, (PFNTHREAD)priority_thread, 0, 2L, 1000);
		}
		// create timer that moves the saver window to top regularly
		WinStartTimer(hab, hwndSaver, IDT_ZORDERTIMER, ZORDERTIMERSTEP);
		return (MRESULT)FALSE;
	case WM_TIMER:
		if(SHORT1FROMMP(mp1) == IDT_ZORDERTIMER){
			// move saver window to top
			WinSetWindowPos(hwnd, HWND_TOP, 0, 0, 0, 0, SWP_ZORDER);
			return (MRESULT)0;
		}
		break;
	case WM_DESTROY:
		// destroy the presentation space
		GpiDestroyPS(hps);
		// stop the z-order timer
		WinStopTimer(hab, hwndSaver, IDT_ZORDERTIMER);
		break;
	case WM_PAINT:
		// just validate the update area. all drawing is done
		// in the drawing-thread.
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
	// data not loaded yet
	       ULONG	size;
	// get name of the saver module (stored as resource string)
	       if(WinLoadString(hab, hmodDLL, IDS_MODULENAME,
				SAVER_NAME_MAXLEN, (PSZ)modulename) == 0){
		   // resource string not found. indicate error by
			  // setting module name to empty string (the name
								  // "" is interpreted as an error by SSDLL.DLL and
								  // the module does not show up in the list box).
			  strcpy(modulename, "");
		   return;
	       }
	// load data from INI-file. the key name is the name of the
	       // saver module
	       size = query_profile_data();
	if(size != sizeof(configuration_data)
	   || configuration_data.version != MODULEVERSION
	   || strncmp(modulename, configuration_data.modulename, sizeof(modulename)) != 0){
	    // if entry is not found or entry has invalid size or
			    // entry has wrong version number, create a new entry
			    // with default values and write it to the INI-file
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
	    configuration_data.count = CONFIGURATION_DEFAULT_COUNT;
	    configuration_data.colors = COLORS_DEFAULT;
	    configuration_data.blank_screen = CONFIGURATION_DEFAULT_BLANKSCREEN;
			

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
		return; 	// don't write if not OS2.INI

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

	The following sample code is from the "Pyramids" module that comes
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
void draw_thread(void *args)
    {
	int i, j;
	const RECTL ScreenRect = { 0, 0, screenSizeX, screenSizeY };
	int color_count = 0;
	ULONG color;
	int pushleftx = 11, pushlefty = 11, pushrightx = 11, pushrighty = 11;
	BOOL	point_buffer_filled;
	BOOL first_paint = TRUE;
	POINTL	*ptleft, *ptright;
	EXCEPTIONREGISTRATIONRECORD xcpthand = { 
	    (PEXCEPTIONREGISTRATIONRECORD)0, GPFHandler };

	DosSetExceptionHandler(&xcpthand) ;

	i = 0;
	point_buffer_filled = FALSE;
	/* allocate memory for circular point buffer	*/
	ptleft = malloc(configuration_data.count * sizeof(POINTL));
	ptright = malloc(configuration_data.count * sizeof(POINTL));
	if( (ptleft == NULL) || (ptright == NULL) ) goto end;
	GpiSetLineWidth( hps, LINEWIDTH_THICK );

	srand(WinGetCurrentTime(hab));
	
	do{
	    color = ((unsigned)rand()) % 17 - 2;
	}while ( color == -1 || color == 0 || color == 7 || color == 8 );
	/* blank screen if requested	*/
	if( configuration_data.blank_screen )
	    WinFillRect( hps, &ScreenRect, CLR_BLACK );

	while(!stop_draw_thread){

	    if(point_buffer_filled){
		/* redraw old line in black	*/
		GpiSetColor(hps, CLR_BLACK);
		GpiMove(hps, &ptleft[i]);
		GpiLine(hps, &ptright[i]);
	    }
	    if( color_count == configuration_data.colors ){
		/* select random color	*/
		do{
		    color = ((unsigned)rand()) % 17 - 2;
		}while ( color == -1 || color == 0 || color == 7 || color == 8 );
		color_count = 0;
	    }
	    GpiSetColor(hps, color);
	    color_count++;	
	    if (first_paint ){		
		/* set the start coordinates	*/
		ptleft[0].x = rand() % screenSizeX;
		ptleft[0].y = rand() % screenSizeY;
		ptright[0].x = rand() % screenSizeX;
		ptright[0].y = rand() % screenSizeY;
		first_paint = FALSE;
	    } else {
		/* calculate new line points	*/
		j = i==0 ? configuration_data.count - 1 : i - 1;
		ptleft[i].x = ptleft[j].x + pushleftx; 
		ptleft[i].y = ptleft[j].y + pushlefty; 
		ptright[i].x = ptright[j].x + pushrightx;
		ptright[i].y = ptright[j].y + pushrighty; 
	    }

	    /* adjust lines if they hit a screen border */
	    if( ptleft[i].x >= screenSizeX ) {
		ptleft[i].x = 2 * screenSizeX - ptleft[i].x;
		pushleftx = - pushleftx + (rand() % 2);
	    }	
	    if( ptleft[i].y >= screenSizeY ) {
		ptleft[i].y = 2 * screenSizeY - ptleft[i].y;
		pushlefty = - pushlefty + (rand() % 2);
	    }	
	    if( ptright[i].x >= screenSizeX ) {
		ptright[i].x = 2 * screenSizeX - ptright[i].x;
		pushrightx = - pushrightx + ( rand() % 2);
	    }	
	    if( ptright[i].y >= screenSizeY ) {
		ptright[i].y = 2 * screenSizeY - ptright[i].y;
		pushrighty = - pushrighty + ( rand() % 2);
	    }
	    if( ptleft[i].x < 0 ) {	
		ptleft[i].x *= -1;
		pushleftx = - pushleftx + (rand() % 2);
	    }	
	    if( ptleft[i].y < 0 ) {
		ptleft[i].y *= -1;
		pushlefty = - pushlefty + (rand() % 2);
	    }	
	    if( ptright[i].x < 0 ) {
		ptright[i].x *= -1;
		pushrightx = - pushrightx + (rand() % 2);
	    }	
	    if( ptright[i].y < 0 ) {
		ptright[i].y *= -1;
		pushrighty = - pushrighty + (rand() % 2);
	    }	
		
	    /* draw line	*/
	    GpiMove(hps, &ptleft[i]);
	    GpiLine(hps, &ptright[i]);

	    /* sleep if necessary	*/
	    if(low_priority == FALSE)
		DosSleep(1);

	    // sleep if user requests slower animation
				 switch(configuration_data.animation_speed){
				 case 4: break;
				 case 3: DosSleep(10); break;
				 case 2: DosSleep(30); break;
				 case 1: DosSleep(50); break;
				 case 0: DosSleep(70); break;
				 }
	    i++;
	    /* move circular buffer index	*/
	    if(i == configuration_data.count){
		point_buffer_filled = TRUE;
		i = 0;
	    }	
	}
	/* free resources	*/
      end:  // goto from memory allocation
	free( ptleft );
	free( ptright );
	DosUnsetExceptionHandler(&xcpthand);
	_endthread();
    }






