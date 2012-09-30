/*
	4colprob.c
	4 Colour Problem saver module C source file for SSaver version 1.2
	(C) 1994 Nicole Greiber
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
#include <float.h>
#include <math.h>

#include "4colprob.h"


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
*/
#define CONFIGURATION_MINIMUM_NUMBER 5
#define CONFIGURATION_DEFAULT_NUMBER 60
#define CONFIGURATION_MAXIMUM_NUMBER 125
// it is not possible to have more points than this, because the program
// creates about twice as much VoronjPoints than StartPoints, and all
// indexing is done with unsigned char's (which are limited to 255)


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
	int	count;
*/
	int number;
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
*/
	static	HWND	hwndCount;
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
		hwndCount = WinWindowFromID(hwnd, IDC_NUMBER);
		WinSendMsg(hwndCount, SPBM_SETLIMITS, (MPARAM)CONFIGURATION_MAXIMUM_NUMBER, (MPARAM)CONFIGURATION_MINIMUM_NUMBER);
		WinSendMsg(hwndCount, SPBM_SETCURRENTVALUE, MPFROMLONG(configuration_data.number), MPVOID);

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
			WinSendMsg(hwndCount, SPBM_QUERYVALUE, MPFROMP(&configuration_data.number), MPFROM2SHORT(0, SPBQ_DONOTUPDATE));

			/* check if the number is in the allowed range	*/
			if( CONFIGURATION_MINIMUM_NUMBER > configuration_data.number ||
				 configuration_data.number > CONFIGURATION_MAXIMUM_NUMBER ) {
				 	sprintf( msgtext, "Sorry, but the number of points must be between %d and %d",
				 				CONFIGURATION_MINIMUM_NUMBER, CONFIGURATION_MAXIMUM_NUMBER );
				 	WinMessageBox( HWND_DESKTOP, hwnd, (PSZ)msgtext, (PSZ)"4 Colour Problem",
										0, MB_OK | MB_INFORMATION | MB_MOVEABLE );
			} else { // everything is o. k.
				// write all configuration data to INI-file
				PrfWriteProfileData(HINI_USER, (PSZ)application_name, (PSZ)modulename, (PSZ)&configuration_data, sizeof(configuration_data));
				// end dialog
				WinDismissDlg(hwnd, TRUE);
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
*/
			configuration_data.number = CONFIGURATION_DEFAULT_NUMBER;

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
	static INT np;						// number of random points
	static INT npp;					// = np + 4, including convex hull
	static POINTL *StartP;		// array for the random points
	static POINTL *VoronjP;	// array for the Voronj points
	static unsigned char **LL, **VV;	// neighbor lists for StartP and VoronjP
	static INT ip;						// actual index for the Voronj points
	static unsigned char *Queue;			// queue for the triangulation
	static INT *Farbe;			// array for the color of each point
	static INT Skala[6] =			// array for the possible field colors
		{ CLR_BLACK, CLR_RED, CLR_BLUE, CLR_YELLOW, CLR_DARKGREEN, CLR_CYAN};

	RECTL pt = {0, 0, screenSizeX, screenSizeY };
											// screen rectangle for blanking
	INT animation_sleep = 4 - configuration_data.animation_speed;

	// the arrays are created as pointers to allow dynamic sizing
	
// Subroutines /////////////////////////////////////////////////

	inline INT Sleep( INT dur )		// sleeps only if the saver stays active
									// returns FALSE if the saver is interrupted
	{
		for(; dur > 0 && !stop_draw_thread; dur -= 10 )
			DosSleep( 10 );
		return !stop_draw_thread;
	}


	VOID DrawPoint( POINTL p, INT Farbe )
			// draws a point 3 pels high and wide
	{
		RECTL r = {p.x - 1, p.y - 1, p.x + 2, p.y + 2};
		WinFillRect( hps, &r, Farbe);
	}

	void DataCreation()	// creates np random points and 4 fixed corner p.
	{
		INT i;

		for( i = 1; i <= np; i++ ){
			do{
				StartP[i].x = rand() % (screenSizeX - 2) + 1;	
				StartP[i].y = rand() % (screenSizeY - 2) + 1;
												//  0 < points < screenSize? - 1
			} while ( GpiQueryPel ( hps, &StartP[i] ) == CLR_WHITE );
			DrawPoint( StartP[i] , CLR_WHITE );
		}	// produces np random points

		StartP[np + 1].x = StartP[np + 3].x = screenSizeX / 2;
		StartP[np + 1].y = StartP[np + 4].x = -5000;
		StartP[np + 2].x = StartP[np + 3].y = 5000;
		StartP[np + 2].y = StartP[np + 4].y = screenSizeY / 2;
	}


	double Theta( POINTL p1, POINTL p2 )
	{
		INT dx, dy, axy;
		double t;
		dx = p2.x - p1.x;
		dy = p2.y - p1.y;
		axy = abs( dx ) + abs( dy );
		if( !axy ){
			t = 0;
		} else {
			t = (double)dy / (double)axy;
		}
		if( dx < 0){
			t = 2 - t;
		} else {
			if( dy < 0 ) t += 4;
		}
		return ( t * 90 );
	}
	
	inline INT Drehsinn( POINTL p1, POINTL p2, POINTL p3 )
	// returns 1 if rotation is clockwise
	{
		INT d = (p2.y - p1.y) * (p2.x + p1.x) + (p3.y - p2.y) * (p3.x + p2.x)
			+ (p1.y - p3.y) * (p1.x + p3.x);

		return ( !d ? 0 : (d<0 ? -1 : 1) );
	}

	inline double SqDist( POINTL p1, POINTL p2 )	// square dist of p1 - p2
	{
		return (p1.x - p2.x)*( p1.x - p2.x) + (p1.y - p2.y)*(p1.y - p2.y);
	}

	double Rho( POINTL p1, POINTL p2, POINTL p3 )
	// returns diameter of surrounding circle
	{
		double r, s, z, a, b, c;
		a = sqrt( SqDist(p1, p2) );
		b = sqrt( SqDist(p2, p3) );
		c = sqrt( SqDist(p1, p3) );
		s = 0.5 * (a + b + c);
		z = s * (s - a)*(s - b)*(s - c);
		if( z < 0.0001){
			r = 5000;
		} else {
			r = 0.25 * a*b*c / sqrt(z);
		}
		if( r > 5000) r = 5000;
		return r;
	}
	
	inline double Cosgam( POINTL p1, POINTL p2, POINTL p3 )
	{
		double a = SqDist(p1, p3),
				 b = SqDist(p2, p3);
		return (a + b - SqDist(p1, p2) )/sqrt(a * b);
	}

	VOID SaveVoronjPoint( unsigned char i, unsigned char j,
										unsigned char k, POINTL pv )
		// adds the index of the Voronjpoint to the voronjpoint-lists of each
		// of the three points that constitute the triangle
		// improved version: checks if the point already exists and only
		// saves it once
	{
		unsigned char ch;
		int itest;
		BOOL found = FALSE;
		for( itest = 1; itest <= ip; itest++)
			if( pv.x == VoronjP[itest].x && pv.y == VoronjP[itest].y ){
				found = TRUE;
				break;
			}
		if( !found ){	// current VoronjPoint is a new one
			VoronjP[++ip] = pv;
			ch = ip;
			strncat( VV[i], &ch, 1);	
			strncat( VV[k], &ch, 1);	
			strncat( VV[j], &ch, 1);
		} else {	// append VoronjPoint-index to neighbourlist if necessary
			ch = itest;
			if( strchr( VV[i], ch ) == NULL )
				strncat( VV[i], &ch, 1);	
			if( strchr( VV[k], ch ) == NULL )
				strncat( VV[k], &ch, 1);	
			if( strchr( VV[j], ch ) == NULL )
				strncat( VV[j], &ch, 1);
		}
	}

	BOOL FindPoint( unsigned char k, unsigned char l, unsigned char *m )
	{
		BOOL found = FALSE;
		unsigned char a; 
		double dx1, dy1, cq, r, w, min = 2;
		POINTL p1, p2, p3;

		p1 = StartP[k];
		p2 = StartP[l];
		
		for( a = 1; a <= npp; a++) {
			if( (a != k) && (a != l) && (Drehsinn( p1, p2, StartP[a] ) == 1)){
				w = Cosgam(p1, p2, StartP[a]);
				if ( w < min ){
					min = w;
					*m = a;
					p3 = StartP[a];
					found = TRUE;
				}
			}	
		}	// for
		if( found && strchr( LL[k], *m ) == NULL
					 && strchr( LL[l], *m ) == NULL ){
			dx1 = p2.x - p1.x;
			dy1 = p2.y - p1.y;
			cq = dx1*dx1 + dy1*dy1;
			r = Rho( p1, p2, p3 );
			w = sqrt( fabs( r*r/cq - 0.25 ));
			if( min < 0) w = -w;
			// now p2 turns into the VoronjPoint:
			p2.x = (INT) rint(0.5*dx1 - dy1*w + p1.x);	
			p2.y = (INT) rint(0.5*dy1 + dx1*w + p1.y);
			SaveVoronjPoint( k, l, *m, p2 );
		}
		return found;
	}

	VOID Triangle()
	{
		unsigned char i, j, k;
		i = Queue[0];
		j = Queue[1];
		strcpy( Queue, &Queue[2] );
		if( strchr( LL[i], j) == NULL){
			if( i <= np && j <= np ){
				strncat( LL[i], &j, 1);
				strncat( LL[j], &i, 1);
				GpiMove( hps, &StartP[i] );
				GpiLine( hps, &StartP[j]);
			}
			if( FindPoint( i, j, &k ) && k <= np){
				strncat( Queue, &k, 1);
				strncat( Queue, &j, 1);
				strncat( Queue, &i, 1);
				strncat( Queue, &k, 1);
			}
		}
	}
	
	VOID DelaunayTriangulation()
	{
		unsigned char i;

		ip = 0;
		for( i = 0; i <=npp; i++ ){
			VV[i][0] = LL[i][0] =  '\0';
			// must be initialized up to npp because the last strings are
			// accessed for comparison
		}
		// initialize the queue with the points of the convex hull
		Queue[7] = Queue[0] = np + 1;
		Queue[2] = Queue[1] = np + 2;
		Queue[4] = Queue[3] = np + 3;
		Queue[6] = Queue[5] = np + 4;
		Queue[8] = '\0';

		GpiSetColor( hps, CLR_PINK);
		while( strlen( Queue ) > 0 ) Triangle();	
	}

	
	VOID OrderPointLists()
	{
		POINTL ptl;
		unsigned char i, j, k, l, m, test, q, size, io[20], Satz[20];
		double w[20];				// array for sorting purposes

		// 20 is enough for the arrays because the VV-strings only have size 20

		VOID Sort( INT li, INT re )	
		{
			INT i = li, j = re, tmp;
			double value = w[io[(li + re) / 2]];

			do{
				while( w[io[i]] < value) i++;
				while( w[io[j]] > value) j--;
				if( i <= j){
					tmp = io[i];
					io[i] = io[j];
					io[j] = tmp;
					i++;
					j--;
				}
			} while (i <= j);
			if (li < j) Sort( li, j);
			if ( i < re) Sort( i, re );
		}	// VOID Sort()

		for( i = 1; i <= np; i++)
		{
			l = strlen( VV[i] );
			for( j = 0; j < l; j++ ) io[j] = j;
			for( j = 0; j < l; j++){
				w[j] = Theta( StartP[i], VoronjP[ (INT)VV[i][j] ] );
			}
			Sort( 0, l-1 );
				// reorder the elements in VV[i] => adjust them according to angle
			for(j = 0; j < l; j++){
				Satz[j] = VV[i][io[j]];
			}
			for(j = 0; j < l; j++){
				VV[i][j] = Satz[j];
			}
		}	// end for i


		// clear the neighborlist from wrong neighbors at the edges:
		// when two StartPoints only share VoronjPoints that are off the
		// screen, then they are no neighbors though they behave as if they
		// were. Therefore these "wrong" neighbors are found out and their
		// neighboring relationship is eliminated

		for( i = 1; i <= np; i++){	// from LL[1] to LL[np]
			l = strlen( LL[i] );
			for( j = 0; j < l; j++ ){	// from LL[i][0] to LL[i][l-1]
				k = LL[i][j];	// i and k should be neighbors, that means they
									// have to share at least one Voronj point
				test = 0;
				q = 0;
				size = strlen( VV[k] );
				while( q < size && !test ){
					if( strchr( VV[i], VV[k][q] ) != NULL ){
						// found a common Voronj point
						ptl = VoronjP[ VV[k][q] ];
						// check if the point is visible:
						if( ptl.x > 0 && ptl.y > 0 && ptl.x < screenSizeX
										  && ptl.y < screenSizeY )
							test = 1;
					}
					q++;
				}
				if( !test ){	// i and k don't share any visible Voronj points
					strcpy( &LL[i][j], &LL[i][j+1] );
						// delete k from i's neighborlist
					m = 0;
					while( LL[k][m] != i ) m++;	// search i in k's neighborlist
					strcpy( &LL[k][m], &LL[k][m+1]);	// and delete it
					l--;	// length of LL[i] has decreased
				}	// end if !test
			}	// end for j
		}	// end for i
		
	}	// end OrderPointLists

	VOID VoronjDiagram( INT Color )
	{
		unsigned char i, j, l;
		POINTL pt = { 0, 0 };
		GpiSetColor( hps, Color );
		GpiSetLineType( hps, LINETYPE_DEFAULT );
		for( i = 1; i <= np; i++){
			l = strlen( VV[i] );	
			GpiMove( hps, &VoronjP[ (INT)VV[i][l-1] ] );
			for( j = 0; j < l; j++)
				GpiLine( hps, &VoronjP[ (INT)VV[i][j] ] );
		}
		// draw a border
		GpiMove( hps, &pt );
		pt.x = screenSizeX - 1;
		GpiLine( hps, &pt );
		pt.y = screenSizeY - 1;
		GpiLine( hps, &pt );
		pt.x = 0;
		GpiLine( hps, &pt );
		pt.y = 0;
		GpiLine( hps, &pt );
	}


	inline VOID PaintField( unsigned char index, INT Color )
	{
		unsigned char j, l = strlen( VV[index] );

		Sleep( 30 * animation_sleep );
		GpiSetColor( hps, Color );
		Farbe[index] = Color;
		GpiBeginPath( hps, 1L );
		GpiMove( hps, &VoronjP[ VV[index][l - 1] ] );
		for( j = 0; j < l; j++ )
			GpiLine( hps, &VoronjP[ VV[index][j] ] );
		GpiEndPath( hps );
		GpiFillPath( hps, 1L, BA_ALTERNATE );	
	}

	inline BOOL FarbeHasColor( unsigned char *index, INT Color )
	{
		unsigned char j = 0, upto = strlen( index );

		for(; j < upto && Farbe[*index++] != Color; j++ );
		return (j != upto);
	}

	VOID FirstPaint( unsigned char i)
	{
		unsigned char j, k, l;

		if( i > np ) i = 1;
		for( j = 1; j <= npp; j++ ) Farbe[j] = CLR_BLACK;	// init Farbe
		do{
			PaintField( i, Skala[1] );	// first field can always have color 1
			l = strlen( LL[i] );
			for( j = 0; j < l; j++ ){
				if( Farbe[ (INT)LL[i][j] ] == CLR_BLACK ){
													// field is yet to be painted
					k = 1;	// red can be left out because i is already red
					while( ++k < 5 && FarbeHasColor(LL[ LL[i][j] ], Skala[k]) );
						// now searches the environment of point LL[i][j]
					PaintField( LL[i][j], Skala[k] );
				}
			}
			i = 0;
			while( Farbe[++i] != CLR_BLACK );
									// searches the next uncolored field
		} while ( i <= np );
	}


	inline BOOL ColorIsAt( unsigned char from, unsigned char to, INT Color,
						 unsigned char *here)
	{
		for(; from <= to && Farbe[from] != Color; from++ );
		*here = from;
		return from <= to;
	}


	VOID SwapColors( unsigned char* m )
	{
		unsigned char i, j, k, l;
		INT Satz[20], Test[20], testlen, pos, Col;

		// 20 is enough for the arrays because the LL-strings only have size 20

		k = *m;
		testlen = l = strlen( LL[k] );
		for( j = 0; j < l; j++)
			Satz[j] = Farbe[ (INT)LL[k][j] ]; // contains the neighbors' colors
		Satz[l] = CLR_BLACK;			// condition to interrupt
		for( j = 0; j <= l; j++) Test[j] = Satz[j];	// Test must be modified
		pos = 0;
		do
		{
			Col = Test[0];
			for( j = 0; j < testlen; j++)
				Test[j] = Test[ j + 1 ];
					// because the last entry of Test contains CLR_BLACK
			testlen--;
			while( pos != testlen && Test[pos] != Col ) pos++;
			if( pos != testlen )	// another neighbour has got colour Col 
			{
				while ( pos != testlen )
				{		// all appearances of Col are deleted from Test
					for( i = pos; i < testlen; i++ ) Test[i] = Test[i+1];
					testlen--;
					while( Test[pos] != Col && pos != testlen) pos++;	// search again
				}
				pos = 0;	// to prevent an accidental interrupt
			}
		} while( Test[0] != CLR_BLACK && pos != testlen );
	// while there are still colors in Test and a color doesn't appear only once
		if( pos == testlen )
		{	// found a color that appears only once ...
			for( i = 0; Satz[i] != Col; i++);
			*m = LL[k][i];		// ... that is here
			for( j = i; j < l - 1; j++ ) LL[k][j] = LL[k][j+1];
			LL[k][l-1] = *m;
			// remove field from LL[k] and add it at the end to provide
			// a different sequence when this field is checked again
			PaintField( k, Col );
			PaintField( *m, CLR_CYAN );
			k = 1;
			while( k < 5 && FarbeHasColor( LL[*m], Skala[k] ) ) k++;
			// check all neighbours' colors and paint the field in an unused color
			PaintField( *m, Skala[k] );
		}
	}

	VOID Reduce()
	{
		unsigned char Hist[256], Wort[16], Start[npp], *Lauf, m;

		Lauf = Start;
		*Lauf++ = '0';		// Start[0] is not needed
		*Lauf++ = '0';		// Start[1] has been used for the first coloring
		for( m = 2; m <= np; m++) *Lauf++ = 'a';	// unused start fields
		*Lauf = '\0';
		
		Wort[0] = Hist[0] = '\0';	// delete track
		
		while( ColorIsAt( 1, np, CLR_CYAN, &m ) )
		{
			if( stop_draw_thread ) return;	// if saver is interrupted
			SwapColors(&m);	// wrong color found
			strncat(Wort, &m, 1);	// m has stayed the same or has got the
						// index of the next field that has been painted light blue
			if( strlen( Wort) > 10 )
			{
				strncat( Hist, &Wort[0], 1 );	// write first Wort-entry to Hist ...
				strcpy( Wort, &Wort[1] );	// ... and delete it from Wort
				if( strlen( Hist) > 250)		// shorten Hist if necessary
					strcpy( Hist, &Hist[1] );
			}
			if ( strstr( Hist, Wort ) != NULL )
			{	// endless loop: Hist contains Wort
				if( strchr( Start, 'a' ) == NULL ) return;	// nothing else possible
				if( Start[m] == '0' )	// this point has already been used to start
					for ( m = 2; Start[m] != 'a'; m++);
						// search first field that has not been used yet for a start
				Start[m] = '0';
				FirstPaint(m);
				Wort[0] = Hist[0] = '\0';	// initialize track anew
			}
		}	// end while( ColorIsAt ... )
	}

// End of Subroutines /////////////////////////////////////////

	// random seed (needed for each thread)
	srand(WinGetCurrentTime(hab));

	npp = configuration_data.number + 4;
		// includes the 4 points of the convex hull
	
	if( !(Queue = (unsigned char*)malloc( 256 * sizeof(unsigned char))) ) stop_draw_thread = TRUE;
	if(!(LL = (unsigned char**)malloc( (npp+1) * sizeof(unsigned char*))) ) stop_draw_thread = TRUE;
	if(!(VV = (unsigned char**)malloc( (npp+1) * sizeof(unsigned char*))) ) stop_draw_thread = TRUE;
	for( np = 0; np <= npp && !stop_draw_thread; np++ ){
		if(!(LL[np] = (unsigned char*)malloc( 20 * sizeof(unsigned char))) ) stop_draw_thread = TRUE;
		if(!(VV[np] = (unsigned char*)malloc( 20 * sizeof(unsigned char))) ) stop_draw_thread = TRUE;
	}	// I use np because it is an existing and here unused variable
	if( !(StartP = (POINTL*)malloc( (npp+1) * sizeof(POINTL))) ) stop_draw_thread = TRUE;
	if( !(VoronjP = (POINTL*)malloc( npp * 2 * sizeof(POINTL))) ) stop_draw_thread = TRUE;
	if( !(Farbe = (INT*)malloc( (npp+1) * sizeof(INT))) ) stop_draw_thread = TRUE;

	np = configuration_data.number;	// from here on np is fixed

	while(!stop_draw_thread){

		WinFillRect( hps, &pt, CLR_BLACK );
		GpiSetColor( hps, CLR_WHITE );

		DataCreation();
		if( Sleep( 800 + 200 * animation_sleep ) ){
			DelaunayTriangulation();		
			if( Sleep( 800 + 200 * animation_sleep ) ){
				OrderPointLists();
				if(!stop_draw_thread){
					VoronjDiagram( CLR_CYAN );
					if( Sleep( 800 + 200 * animation_sleep ) ){
						FirstPaint(1);	
						if( Sleep( 400 + 100 * animation_sleep ) ){
							Reduce();
							Sleep( 2500 + 500 * animation_sleep );
						}
					}
				}
			}
		}

		// sleep if necessary
		if(low_priority == FALSE)
			DosSleep(1);

	}	/*	stop_draw_thread	*/

	// free resources

	_endthread();
}
