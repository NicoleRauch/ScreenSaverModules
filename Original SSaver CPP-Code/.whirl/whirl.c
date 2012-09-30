/*
	sample.c
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
#include <math.h>
#include <float.h>

#include "whirl.h"


// ===== preprocessor definitions

#define MODULEVERSION		0x00010002
#define STACKSIZE		32000
#define SAVER_NAME_MAXLEN	32
#define FUNCTION_CONFIGURE	1
#define FUNCTION_STARTSAVER	2
#define FUNCTION_STOPSAVER	3
#define FUNCTION_QUERYNAME	4
#define FUNCTION_QUERYENABLED	5
#define FUNCTION_SETENABLED	6
#define CONFIGURATION_DEFAULT_ANIMATIONSPEED 4	// 0=slow .. 4=fast
/*
	$$$$$ insert code here $$$$$
	This is the place for your preprocessor definitions.

	$$$$$ for example $$$$$
#define CONFIGURATION_MINIMUM_COUNT 1
#define CONFIGURATION_DEFAULT_COUNT 32
#define CONFIGURATION_MAXIMUM_COUNT 100
*/

// ===== prototypes

void	EXPENTRY SAVER_PROC(int function, HAB _hab, HWND hwndOwner, char *appname, void *buffer);
static	MRESULT EXPENTRY SaverWindowProc(HWND hwnd, ULONG msg, MPARAM mp1, MPARAM mp2);
static	MRESULT EXPENTRY ConfigureDlgProc(HWND hwnd, ULONG msg, MPARAM mp1, MPARAM mp2);
static	void	draw_thread(void *args);
static	void	priority_thread(void *args);
static	void	load_configuration_data(void);


// ===== global data

ULONG	MODULESOURCE_VERSION = 0x00010002;	// EXPORTED: ScreenSaver 1.2 module source code (do not change)

static	LONG	screenSizeX = 0;		// screen size x
static	LONG	screenSizeY = 0;		// screen size y
static	HWND	hwndSaver = NULLHANDLE; 	// saver window handle
static	HMODULE hmodDLL = NULLHANDLE;		// saver module dll handle
static	char	*application_name;		// name of ScreenSaver app
static	TID	tidDraw;			// drawing-thread ID
static	TID	tidPriority;			// priority regulation thread ID
static	HPS	hps;				// presentation space handle
static	HAB	hab;				// anchor block handle
static	BOOL	low_priority = TRUE;		// low-priority flag
static	BOOL	configuration_data_loaded = FALSE; // config data loaded flag
static	volatile BOOL stop_draw_thread; 	// stop flag
static	char	modulename[SAVER_NAME_MAXLEN+1]; // module name buffer
static	volatile BOOL stop_priority_thread = FALSE;

static	struct	_configuration_data {
	ULONG	version;
	BOOL	enabled;
	int	animation_speed;
/*
	$$$$$ insert code here $$$$$
	If your saver module needs some additional configuration data,
	insert it here. It is automatically loaded and saved.

	$$$$$ for example $$$$$
*/
	int	sign;

} configuration_data;


// ===== code

/*
	SAVER_PROC
	This is the entry point into the saver module that is called by
	the ScreenSaver program.
	There should be no reason to alter the code.
	Depending on the requested function, the following tasks
	are performed:
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
	// load all configuration data from INI-file
	load_configuration_data();
	switch(function){
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
		PrfWriteProfileData(HINI_USER, (PSZ)application_name, (PSZ)modulename, (PSZ)&configuration_data, sizeof(configuration_data));
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
	static	HWND	hwndCount;
*/
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
		hwndCount = WinWindowFromID(hwnd, IDC_COUNT);
		WinSendMsg(hwndCount, SPBM_SETLIMITS, (MPARAM)CONFIGURATION_MAXIMUM_COUNT, (MPARAM)CONFIGURATION_MINIMUM_COUNT);
		WinSendMsg(hwndCount, SPBM_SETCURRENTVALUE, MPFROMLONG(configuration_data.count), MPVOID);
*/
		(VOID)WinCheckButton( hwnd, (USHORT)IDC_CLOCK, configuration_data.sign == -1 );
		(VOID)WinCheckButton( hwnd, (USHORT)IDC_COUNTER, configuration_data.sign == 1 );

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
			WinSendMsg(hwndCount, SPBM_QUERYVALUE, MPFROMP(&configuration_data.count), MPFROM2SHORT(0, SPBQ_DONOTUPDATE));
*/
			if( WinQueryButtonCheckstate( hwnd, IDC_CLOCK ) == 1 )
				configuration_data.sign = -1;
			else
				configuration_data.sign = 1;
			
			// write all configuration data to INI-file
			PrfWriteProfileData(HINI_USER, (PSZ)application_name, (PSZ)modulename, (PSZ)&configuration_data, sizeof(configuration_data));
			// end dialog
			WinDismissDlg(hwnd, TRUE);
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
	switch(msg){
	case WM_CREATE:
		// reset the "stop" flag
		stop_draw_thread = FALSE;
		// store window handle
		hwndSaver = hwnd;
		// get presentation space
		hps = WinGetPS(hwnd);
		// start the drawing-thread
		tidDraw = _beginthread(draw_thread, NULL, STACKSIZE, NULL);
		// create thread to control priority of drawing thread
		if(low_priority){
			stop_priority_thread = FALSE;
			DosCreateThread(&tidPriority, (PFNTHREAD)priority_thread, 0, 2L, 1000);
		}
		return (MRESULT)FALSE;
	case WM_DESTROY:
		// release the presentation space
		WinReleasePS(hps);
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
		BOOL	fSuccess;
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
		size = sizeof(configuration_data);
		fSuccess = PrfQueryProfileData(HINI_USER,
		  (PSZ)application_name, (PSZ)modulename,
		  (PSZ)&configuration_data, &size);
		if(!fSuccess || size != sizeof(configuration_data) || configuration_data.version != MODULEVERSION){
			// if entry is not found or entry has invalid size or
			// entry has wrong version number, create a new entry
			// with default values and write it to the INI-file
			configuration_data.version = MODULEVERSION;
			configuration_data.enabled = TRUE;
			configuration_data.animation_speed = CONFIGURATION_DEFAULT_ANIMATIONSPEED;
/*
			$$$$$ insert code here $$$$$
			If you have added data to the configuration_data
			structure, insert code here to set the default
			values for your data items.

			$$$$$ for example $$$$$
			configuration_data.count = CONFIGURATION_DEFAULT_COUNT;
*/
			configuration_data.sign = -1;
			PrfWriteProfileData(HINI_USER, (PSZ)application_name, (PSZ)modulename, (PSZ)&configuration_data, sizeof(configuration_data));
		}
		configuration_data_loaded = TRUE;
	}
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
void	draw_thread(void *args)
{
	POINTL	BitmapPoints[4] = { 1, 1, screenSizeX + 1, screenSizeY + 1,
											0, 0, screenSizeX - 1, screenSizeY - 1 };
	POINTL Centerold, Centernew,
			 Screencenter = {screenSizeX / 2, screenSizeY / 2};
	INT alpha, phi, i, j, sign = configuration_data.sign;
	const ARCPARAMS arcparams = { 10, 10, 0, 0 };

	GpiSetArcParams( hps, &arcparams );
	GpiMove( hps, &Screencenter );
	GpiSetColor( hps, CLR_BLACK );
	GpiFullArc( hps, DRO_FILL, MAKEFIXED( 1, 0) );
	while(!stop_draw_thread){
		for( phi = 360; phi > 0 && phi < 720; phi -= sign * 50 ){
			for( i = Screencenter.y * 2; i >= 10; i /= 1.05 /* 1.5 */ ){
				alpha = phi;
				for( j = 1; j <= 20; j++ ){	/* different places to move	*/
					Centerold.x = Screencenter.x + i * cos( alpha * M_PI / 180 );
					Centerold.y = Screencenter.y + i * sin( alpha * M_PI / 180 );
					BitmapPoints[2].x = Centerold.x - i / 3;
					BitmapPoints[2].y = Centerold.y - i / 4;
					BitmapPoints[3].x = Centerold.x + i / 3;
					BitmapPoints[3].y = Centerold.y + i / 4;

					alpha += 10 * sign;
					Centernew.x = Screencenter.x +
						i*(1 - sign*(360 - phi)/7000) * cos( alpha * M_PI / 180 );
					Centernew.y = Screencenter.y +
						i*(1 - sign*(360 - phi)/7000) * sin( alpha * M_PI / 180 );
					BitmapPoints[0].x = Centernew.x - i / 3;
					BitmapPoints[0].y = Centernew.y - i / 4;
					BitmapPoints[1].x = Centernew.x + i / 3;
					BitmapPoints[1].y = Centernew.y + i / 4;

					GpiBitBlt( hps, hps, 3, BitmapPoints, ROP_SRCCOPY, BBO_IGNORE );
					alpha -= /* 100*/ 28 * sign;
					if (stop_draw_thread){
						phi = - 360;
						i = 0;
						j = 100;
					}

					// sleep if necessary
					if(low_priority == FALSE)
						DosSleep(1);

					// sleep if user requests slower animation
					switch(configuration_data.animation_speed){
						case 4: break;
						case 3: DosSleep(10); break;
						case 2: DosSleep(30); break;
						case 1: DosSleep(50); break;
						case 0: DosSleep(70); break;
					}	/* switch	*/
				}	/* for j 	*/
			}	/* for i 	*/
		}		/* for phi	*/
	}			/* while(! ...	*/

	// free resources

	_endthread();
}
