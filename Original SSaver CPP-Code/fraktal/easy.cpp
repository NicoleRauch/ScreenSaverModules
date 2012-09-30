#include "easy.h"

static int window_invalid=0;
static Window *actwindow;

Program program;

// ******************* Debug tools **********************

void dumplong (long n)
{	char s[256];
	sprintf(s,"%ld, %lx",n,n);
	WinMessageBox(HWND_DESKTOP,HWND_DESKTOP,s,"Dump",0,
		MB_OK);
}

void dump (char *s)
{	WinMessageBox(HWND_DESKTOP,HWND_DESKTOP,s,"Dump",0,
		MB_OK);
}

// *********** Messages etc. ***************************

void Message (char *s, char *title)
{	WinMessageBox(HWND_DESKTOP,HWND_DESKTOP,s,title,0,
		MB_OK|MB_INFORMATION);
}

void Warning (char *s, char *title)
{	WinMessageBox(HWND_DESKTOP,HWND_DESKTOP,s,title,0,
		MB_OK|MB_WARNING);
}

int Question (char *s, char *title)
{	return (WinMessageBox(HWND_DESKTOP,HWND_DESKTOP,s,title,0,
		MB_YESNO|MB_QUERY)==MBID_YES);
}

// **************** Threads **********************

void ThreadProc (Thread *s)
{	int ret=s->call();
	DosExit(EXIT_THREAD,ret);
}

void Thread::start (Parameter p)
{   stop();
	P=p;
	DosCreateThread(&Tid,(PFNTHREAD)ThreadProc,
		Parameter(this),0,Stacksize);
	Started=1;
}

void Thread::stop ()
{	if (Started) DosKillThread(Tid);
	Started=0;
}

void Thread::suspend ()
{	if (Started) DosSuspendThread(Tid);
}

void Thread::resume ()
{	if (Started) DosResumeThread(Tid);
}

void Thread::wait ()
{	if (Started) DosWaitThread(&Tid,DCWW_WAIT);
}

// **************** Time *************************

void Time::set ()
{	DosQuerySysInfo(QSV_TIME_LOW,QSV_TIME_LOW,
		&Seconds,sizeof(ULONG));
}

// ********************* Strings ************************

long defaultsize=256;

String::String (char *text, LONG size)
{	P=new char[size];
	strcpy(P,text);
	Size=size;
}

String::String (int id)
{   char s[1024];
	WinLoadString(program.hab(),NULLHANDLE,id,1022,s);
	Size=strlen(s)+1;
	P=new char[Size];
	strcpy(P,s);
}


String::String ()
{   Size=defaultsize;
	P=new char[Size];
	*P=0;
}

String::~String ()
{	delete P;
}

void String::copy (char *text)
{	delete P;
	Size=strlen(text)+1;
	P=new char[Size];
	strcpy(P,text);
}

char *String::filename ()
{	char *p=P+strlen(P);
	while (p>P)
	{	if (*p=='\\' || *p==':') return p+1;
		p--;
	}
	return p;
}

void String::stripfilename ()
{	*filename()=0;
}

// ****************** Rectangle *************************

void Rectangle::minsize (LONG wmin, LONG hmin)
{	if (W<0 && -W<wmin) W=-wmin;
	else if (W>=0 && W<wmin) W=wmin;
	if (H<0 && -H<hmin) H=-hmin;
	else if (H>=0 && H<hmin) H=hmin;
}

void Rectangle::hrescale (double scale)
{	H=(LONG)(scale*abs(W))*(H<0?-1:1);
}

void Rectangle::wrescale (double scale)
{	W=(LONG)(scale*abs(H))*(W<0?-1:1);
}

// ******************** Program *************************

MRESULT EXPENTRY MainWindowProc (HWND hwnd, ULONG msg,
	MPARAM mp1, MPARAM mp2)
{	Window *window;
	clicktype click;
	if (window_invalid) window=actwindow;
	else window=(Window *)WinQueryWindowPtr(hwnd,QWL_USER);
	POINTS *points;
	switch (msg)
	{   case WM_CREATE :
			window->Handle=hwnd;
			break;
		case WM_PAINT :
			{	RedrawPS ps(hwnd,*window);
				window->redraw(ps);
			}
			break;
		case WM_SIZE :
			if (SHORT1FROMMP(mp2)==0) goto def;
			if (window->Width==SHORT1FROMMP(mp2) &&
				window->Height==SHORT2FROMMP(mp2)) goto def;
			window->Width=SHORT1FROMMP(mp2);
			window->Height=SHORT2FROMMP(mp2);
			window->sized();
			break;
		case WM_BUTTON1CLICK :
			click=button1; goto button;
		case WM_BUTTON2CLICK :
			click=button2; goto button;
		case WM_BUTTON3CLICK :
			click=button3; goto button;
		case WM_BUTTON1UP :
			click=button1up; goto button;
		case WM_BUTTON2UP :
			click=button2up; goto button;
		case WM_BUTTON3UP :
			click=button3up; goto button;
		case WM_BUTTON1DOWN :
			click=button1down; goto button;
		case WM_BUTTON2DOWN :
			click=button2down; goto button;
		case WM_BUTTON3DOWN :
			click=button3down; goto button;
		case WM_BUTTON1DBLCLK :
			click=button1double; goto button;
		case WM_BUTTON2DBLCLK :
			click=button2double; goto button;
		case WM_MOUSEMOVE :
			click=mousemove; goto button;
		case WM_BUTTON3DBLCLK :
			click=button3double;
			button: points=(POINTS *)&mp1;
			window->clicked(points->x,points->y,click);
			goto def;
		case WM_COMMAND :
			if (window->Windowmenu)
				if (window->Windowmenu->call(SHORT1FROMMP(mp1)))
					break;
			goto def;
		case WM_SHOW :
			window->Visible=SHORT1FROMMP(mp1);
			goto def;
		default :
			def: return WinDefWindowProc(hwnd,msg,mp1,mp2);
	}
	return (MRESULT)FALSE;
}

Program::Program ()
{	Hab=WinInitialize(0);
	Hmq=WinCreateMsgQueue(Hab,0);
	WinRegisterClass(Hab,"MainWindow",MainWindowProc,
		CS_SIZEREDRAW,sizeof(Window *));
}

Program::~Program ()
{	WinDestroyMsgQueue(Hmq);
	WinTerminate(Hab);
}

inline int Program::getmessage ()
{	return WinGetMsg(Hab,&Qmsg,0L,0,0);
}

inline void Program::dispatch ()
{	WinDispatchMsg(Hab,&Qmsg);
}

void Program::loop ()
{	while(getmessage())
		dispatch();
}

Window::Window (int id, char *name, ULONG flags)
{	Id=id; Name=new String(name); Flags=flags;
}

void Window::init ()
{   window_invalid=1;
	actwindow=this;
	FrameHandle=WinCreateStdWindow(HWND_DESKTOP,
		WS_VISIBLE,&Flags,"MainWindow",
		Name->text(),WS_VISIBLE,(HMODULE)0,Id,&Handle);
	WinSetWindowPtr(Handle,QWL_USER,this);
	window_invalid=0;
	Windowmenu=NULL;
	Visible=1;
	Width=Height=0;
}

Window::~Window ()
{	WinDestroyWindow(FrameHandle);
}

void Window::size (LONG w, LONG h)
{   RECTL r;
	r.xLeft=r.yBottom=0; r.xRight=w; r.yTop=h;
	WinCalcFrameRect(FrameHandle,&r,FALSE);
	WinSetWindowPos(FrameHandle,NULLHANDLE,0,0,
		r.xRight,r.yTop,SWP_SIZE);
}

void Window::top ()
{	WinSetWindowPos(FrameHandle,
		HWND_TOP,0,0,0,0,SWP_ZORDER);
	WinSetFocus(HWND_DESKTOP,Handle);
}

int Window::rubberbox (LONG x, LONG y, clicktype click,
	Rectangle &R, LONG wmin, LONG hmin, double wscale, double hscale)
{   WindowPS ps(*this);
	ps.mix(FM_XOR);
	switch (click)
	{	case button1down :
			Rubber=1;
			R=Rectangle(x,y,wmin,hmin);
			ps.frame(R,0,CLR_GREEN);
			return RUBBER_START;
		case mousemove :
			if (!Rubber) break;
			ps.frame(R,0,CLR_GREEN);
			R.resize(x-R.x(),y-R.y());
			R.minsize(hmin,wmin);
			if (wscale>0) R.wrescale(wscale);
			if (hscale>0) R.hrescale(hscale);
			ps.frame(R,0,CLR_GREEN);
			Rubber=2;
			break;
		case button1up :
			if (!Rubber || Rubber==1) return RUBBER_CANCEL;
			ps.frame(R,0,CLR_GREEN);
			Rubber=0;
			if (abs(y-R.y())>=hmin) return RUBBER_DONE;
			else return RUBBER_CANCEL;
	}
	return RUBBER_ZERO;
}

// ************* Menus ************************************

Menu::~Menu ()
{	while (Mp)
	{	delete Mp;
		Mp=Mp->next();
	}
}

int Menu::call (int command)
{	Menuentry *m=Mp;
	while (m)
		if (m->command()==command)
		{	m->call(command);
			return 1;
		}
		else m=m->next();
	return 0;
}

// ************* Presentation Space class (PS) *************

void PS::setcolor (int index, Rgb rgb, int pure)
{	LONG table[1];
	table[0]=rgb;
	GpiCreateLogColorTable(Handle,pure?LCOL_PURECOLOR:0,
				LCOLF_CONSECRGB,index,1,(PLONG)table);
}

void PS::directcolor (int pure)
{	GpiCreateLogColorTable(Handle,pure?LCOL_PURECOLOR:0,
				LCOLF_RGB,0,0,NULL);
}

void PS::defaultcolors ()
{	GpiCreateLogColorTable(Handle,LCOL_RESET,0,0,0,NULL);
}

void PS::move (LONG c, LONG r, ULONG col)
{   color(col);
	P.x=c; P.y=r; GpiMove(Handle,&P);
}

void PS::linerel (LONG w, LONG h, ULONG col)
{   color(col);
	P.x+=w; P.y+=h; GpiLine(Handle,&P);
}

void PS::lineto (LONG c, LONG r, ULONG col)
{   color(col);
	P.x=c; P.y=r; GpiLine(Handle,&P);
}

void PS::point (LONG c, LONG r, ULONG col)
{   color(col);
	P.x=c; P.y=r; GpiSetPel(Handle,&P);
}

void PS::text (char *s, ULONG col, ULONG al)
{	color(col);
	alignment(al);
	GpiCharString(Handle,strlen(s),s);
}

void PS::frame (LONG w, LONG h, int r, ULONG col)
{	color(col);
	P.x+=w+1; P.y+=h+1; GpiBox(Handle,DRO_OUTLINE,&P,r,r);
}

void PS::area (LONG w, LONG h, int r, ULONG col)
{	color(col);
	GpiSetBackMix(Handle,BM_OVERPAINT);
	GpiSetPattern(Handle,PATSYM_SOLID);
	P.x+=w+1; P.y+=h+1; GpiBox(Handle,DRO_FILL,&P,r,r);
}

void PS::framedarea (LONG w, LONG h, int r, ULONG col)
{	color(col);
	GpiSetBackMix(Handle,BM_OVERPAINT);
	GpiSetPattern(Handle,PATSYM_BLANK);
	P.x+=w+1; P.y+=h+1; GpiBox(Handle,DRO_OUTLINEFILL,&P,r,r);
}

void PS::frame (Rectangle &R, int r, ULONG col)
{	move(R.x1(),R.y1());
	frame(abs(R.w()),abs(R.h()),r,col);
}

void PS::area (Rectangle &R, int r, ULONG col)
{	move(R.x1(),R.y1());
	area(abs(R.w()),abs(R.h()),r,col);
}

void PS::framedarea (Rectangle &R, int r, ULONG col)
{	move(R.x1(),R.y1());
	framedarea(abs(R.w()),abs(R.h()),r,col);
}

//************ Bitmaps Presentation Spaces *****************

BitmapPS::BitmapPS (Window &window)
{	PSZ pszData[4]={"Display",NULL,NULL,NULL};
	BITMAPINFOHEADER2 bmp;
	LONG alData[2],size;
	DeviceHandle=DevOpenDC(program.hab(),OD_MEMORY,"*",4,
		(PDEVOPENDATA)pszData,NULLHANDLE);
	S.cx=window.width(); S.cy=window.height();
	Handle=GpiCreatePS(program.hab(),DeviceHandle,&S,
		PU_PELS|GPIA_ASSOC|GPIT_MICRO);
	GpiQueryDeviceBitmapFormats(Handle,2,(PLONG)alData);
	bmp.cbFix=(ULONG)sizeof(BITMAPINFOHEADER2);
	bmp.cx=S.cx; bmp.cy=S.cy;
	Planes=bmp.cPlanes=alData[0];
	Colorbits=bmp.cBitCount=alData[1];
	bmp.ulCompression=BCA_UNCOMP;
	bmp.cbImage=(((S.cx*(1<<bmp.cPlanes)*(1<<bmp.cBitCount))+31)
		/32)*S.cy;
	bmp.cxResolution=70; bmp.cyResolution=70;
	bmp.cclrUsed=2; bmp.cclrImportant=0;
	bmp.usUnits=BRU_METRIC; bmp.usReserved=0;
	bmp.usRecording=BRA_BOTTOMUP; bmp.usRendering=BRH_NOTHALFTONED;
	bmp.cSize1=0; bmp.cSize2=0;
	bmp.ulColorEncoding=BCE_RGB; bmp.ulIdentifier=0;
	size=sizeof(BITMAPINFO2)+
		(sizeof(RGB2)*(1<<bmp.cPlanes)*(1<<bmp.cBitCount));
	if (DosAllocMem((PVOID *)&Info,size,PAG_COMMIT|PAG_READ|PAG_WRITE))
	{	Valid=0;
		GpiDestroyPS(Handle);
		DevCloseDC(DeviceHandle);
	}
	else Valid=1;
	Info->cbFix=bmp.cbFix;
	Info->cx=bmp.cx; Info->cy=bmp.cy;
	Info->cPlanes=bmp.cPlanes; Info->cBitCount=bmp.cBitCount;
	Info->ulCompression=BCA_UNCOMP;
	Info->cbImage=((S.cx+31)/32)*S.cy;
	Info->cxResolution=70; Info->cyResolution=70;
	Info->cclrUsed=2; Info->cclrImportant=0;
	Info->usUnits=BRU_METRIC;
	Info->usReserved=0;
	Info->usRecording=BRA_BOTTOMUP;
	Info->usRendering=BRH_NOTHALFTONED;
	Info->cSize1=0; Info->cSize2=0;
	Info->ulColorEncoding=BCE_RGB; Info->ulIdentifier=0;
	BitmapHandle=GpiCreateBitmap(Handle,&bmp,FALSE,NULL,Info);
	GpiSetBitmap(Handle,BitmapHandle);
	GpiErase(Handle);
}

BitmapPS::~BitmapPS ()
{   if (!Valid) return;
	GpiDeleteBitmap(BitmapHandle);
	DosFreeMem(Info);
	GpiDestroyPS(Handle);
	DevCloseDC(DeviceHandle);
}

void BitmapPS::copy (PS &ps)
{	POINTL a[4];
	RECTL r;
	r.xLeft=a[0].x=0; r.yBottom=a[0].y=0;
	r.xRight=a[1].x=S.cx; r.yTop=a[1].y=S.cy;
	a[2].x=0; a[2].y=0; a[3].x=S.cx; a[3].y=S.cy;
	if (!GpiRectVisible(ps.handle(),&r)) return;
	GpiBitBlt(ps.handle(),Handle,4,a,ROP_SRCCOPY,BBO_IGNORE);
}

void BitmapPS::save (char *filename)
{	FILE *f;
	BITMAPFILEHEADER2 bfh;
	char *buffer;
	PBITMAPINFO2 pbmi;
	long size,colorsize,hdsize=20;
	ULONG compression=BCA_UNCOMP;
	f=fopen(filename,"wb");
	if (!f)
	{   Warning("Could not open\nthat file.","Save Error");
		return;
	}
	bfh.usType=BFT_BMAP;
	colorsize=((Colorbits<=8)?(1<<Colorbits):256)*sizeof(RGB2);
	size=((width()*Colorbits+31)/32)*4*height();
	bfh.offBits=14+hdsize+colorsize;
	bfh.cbSize=14+hdsize+colorsize+size;
	buffer=(char *)malloc(size);
	pbmi=(PBITMAPINFO2)malloc(16+colorsize);
	memset(pbmi,0,sizeof(BITMAPINFOHEADER2));
	pbmi->cbFix=hdsize;
	pbmi->cPlanes=1;
	pbmi->cBitCount=Colorbits;
	pbmi->ulCompression=compression;
	if (GpiQueryBitmapBits(handle(),0,height(),buffer,pbmi)!=height())
	{   Warning("Bitmap Save Error!","Error");
		goto end;
	}
	fwrite((char *)&bfh,14,1,f);
	fwrite((char *)pbmi,hdsize+colorsize,1,f);
	fwrite(buffer,1,size,f);
	end: free(pbmi);
	free(buffer);
	fclose(f);
}

//******************* Fonts ********************************

Font::Font (PS &ps)
{   Ps=&ps;
	Fm=(FONTMETRICS *)malloc(sizeof(FONTMETRICS));
	GpiQueryFontMetrics(Ps->handle(),sizeof(FONTMETRICS),Fm);
}

Font::Font ()
{	Ps=NULL; Fm=NULL;
}

Font::~Font ()
{	if (Fm) free(Fm);
}

void Font::set (PS &ps)
{   Ps=&ps;
	if (Fm) free(Fm);
	Fm=(FONTMETRICS *)malloc(sizeof(FONTMETRICS));
	GpiQueryFontMetrics(Ps->handle(),sizeof(FONTMETRICS),Fm);
}


void Font::text (int c, int r, char *s, ULONG col)
// Draw a Text at char column and row
{	Ps->move(c*wbox(),Ps->height()-r*hbox()-above());
	Ps->text(s,col);
}

void Font::centertext (int c, int r, int w, char *s, ULONG col)
// Draw a Text at char column and row
{	Ps->move((2*c+w)*wbox()/2,Ps->height()-r*hbox()-above());
	Ps->text(s,col,TA_CENTER);
}

void Font::righttext (int c, int r, int w, char *s, ULONG col)
// Draw a Text at char column and row
{	Ps->move((c+w)*wbox(),Ps->height()-r*hbox()-above());
	Ps->text(s,col,TA_RIGHT);
}

void Font::textframe (int c, int r, int w, int h, int rad,
	ULONG col, int framed)
// Draw a frame around a text area
{	Ps->move(c*wbox()-wbox()/2,Ps->height()-(r+h+1)*hbox()+hbox()/2);
	if (framed) Ps->framedarea((w+1)*wbox(),(h+1)*hbox(),rad,col);
	else Ps->area((w+1)*wbox(),(h+1)*hbox(),rad,CLR_WHITE);
}

int Font::inframe (int c, int r, int w, int h, LONG x, LONG y)
{	LONG c1,c2,r1,r2;
	c1=c*wbox()-wbox()/2; c2=c1+(w+1)*wbox();
	r1=Ps->height()-(r+h+1)*hbox()+hbox()/2; r2=r1+(h+1)*hbox();
	return (x>c1 && x<c2 && y>r1 && y<r2);
}

void Font::getframe (int c, int r, int w, int h,
	LONG &x1, LONG &y1, LONG &x2, LONG &y2)
{	x1=c*wbox()-wbox()/2; x2=x1+(w+1)*wbox();
	y1=Ps->height()-(r+h+1)*hbox()+hbox()/2; y2=y1+(h+1)*hbox();
}

int Font::line (int r, int h, LONG y)
{	return (Ps->height()-r*hbox()-y)/hbox();
}

// *********************** Text Box **************************

void Textbox::redraw ()
{   char *p=Text,*q,c;
	int line=0;
	if (!*F) return;
	F->textframe(X,Y,W,H,Rad,Color,Framed);
	while (1)
	{   q=p; while (*q && *q!='\n') q++;
		c=*q; *q=0;
		switch (Justify)
		{   case text_left : F->text(X,Y+line,p,Color); break;
			case text_right : F->righttext(X,Y+line,W,p,Color); break;
			default : F->centertext(X,Y+line,W,p,Color); break;
		}
		if (!c) break;
		*q=c;
		p=q+1;
		line++;
	}
}

//******************* Help ******************************

Help::Help (Window &window, int id, char *filename, char *title)
{	HELPINIT hini;
	hini.cb=sizeof(HELPINIT);
	hini.ulReturnCode=0L;
	hini.pszTutorialName=(PSZ)NULL;
	hini.phtHelpTable=
		(PHELPTABLE)MAKELONG(id,0xFFFF);
	hini.hmodHelpTableModule=(HMODULE)0;
	hini.hmodAccelActionBarModule=(HMODULE)0;
	hini.idAccelTable=0;
	hini.idActionBar=0;
	hini.pszHelpWindowTitle=title;
	hini.fShowPanelId = CMIC_HIDE_PANEL_ID;
	hini.pszHelpLibraryName=filename;
	Handle=WinCreateHelpInstance(program.hab(),&hini);
	if (!Handle) { Valid=0; return; }
	Valid=1;
	WinAssociateHelpInstance(Handle,window.framehandle());
}

// ****************** Dialogs *************************

Dialog *activedlg;

static MRESULT EXPENTRY dialogproc (HWND hwnd, ULONG msg,
	MPARAM mp1, MPARAM mp2)
{   Dialogitem *item=activedlg->Items;
	switch (msg)
	{   case WM_INITDLG :
			activedlg->Handle=hwnd;
			while (item)
			{	item->init();
				item=item->next();
			}
			activedlg->start();
			break;
		case WM_COMMAND :
			activedlg->Result=(SHORT1FROMMP(mp1));
			if (activedlg->handler(SHORT1FROMMP(mp1)))
			{	while (item)
				{	item->exit();
					item=item->next();
				}
				activedlg->stop();
				WinDismissDlg(hwnd,SHORT1FROMMP(mp1));
			}
			break;
		default :
			return WinDefDlgProc(hwnd,msg,mp1,mp2);
	}
	return (MRESULT)FALSE;
}

Dialog::Dialog (Window &window, int id) : S()
{	W=&window;
	Id=id;
	Items=0;
}

Dialogitem *Dialog::entry (Dialogitem *item)
{	Dialogitem *note=Items;
	Items=item;
	return note;
}

void Dialog::carryout ()
{   activedlg=this;
	Handle=WinDlgBox(HWND_DESKTOP,W->handle(),dialogproc,
		(HMODULE)0,Id,NULL);
}

int Dialog::handler (int com)
{	if (com==DID_OK) return 1;
	return 0;
}

char *Dialog::gettext (int id, char *text, long size)
{   HWND handle=WinWindowFromID(Handle,id);
	WinQueryWindowText(handle,size,text);
	return text;
}

char *Dialog::gettext (int id)
{   HWND handle=WinWindowFromID(Handle,id);
	WinQueryWindowText(handle,S.size(),(PSZ)S.text());
	return S;
}

void Dialog::settext (int id, char *text)
{	HWND handle=WinWindowFromID(Handle,id);
	WinSetWindowText(handle,text);
}

MRESULT Dialog::message (int id, int msg,
	Parameter mp1, Parameter mp2)
{	HWND h;
	h=WinWindowFromID(Handle,id);
	return WinSendMsg(h,msg,mp1,mp2);
}

//*************** Dialog Items ******************

Dialogitem::Dialogitem (int id, Dialog &dialog)
{	Id=id;
	D=&dialog;
	Next=dialog.entry(this);
}

void StringItem::init ()
{	D->settext(Id,S);
}

void StringItem::exit ()
{	S.copy(D->gettext(Id));
}

void DoubleItem::init ()
{	sprintf(S,"%-0.10g",X);
	D->settext(Id,S);
}

void DoubleItem::exit ()
{ 	S.copy(D->gettext(Id));
	sscanf(S,"%lg",&X);
}

void LongItem::init ()
{	sprintf(S,"%-ld",N);
	D->settext(Id,S);
}

void LongItem::exit ()
{ 	S.copy(D->gettext(Id));
	sscanf(S,"%ld",&N);
}

void SpinItem::init ()
{	D->message(Id,SPBM_SETLIMITS,Upper,Lower);
	D->message(Id,SPBM_SETCURRENTVALUE,N);
}

void SpinItem::exit ()
{	D->message(Id,SPBM_QUERYVALUE,&N,Parameter(0,SPBQ_ALWAYSUPDATE));
}

//******************* File Selector ********************

FileSelector::FileSelector (Window &window,
		char *filter, int saving,
		char *title, char *ok) : Filter(filter)
{	memset(&Fd,0,sizeof(FILEDLG));
	Fd.cbSize=sizeof(FILEDLG);
	Fd.fl=FDS_CENTER|FDS_ENABLEFILELB
		|(saving?FDS_SAVEAS_DIALOG:FDS_OPEN_DIALOG);
	Fd.pszTitle=title;
	if (!ok)
	{	if (saving) ok="Save";
		else ok="Load";
	}
	Fd.pszOKButton=ok;
	W=&window;
}

char *FileSelector::select ()
{   char *p=Filter.filename();
	String s(Fd.szFullFile,256);
	s.stripfilename();
	strcpy(Fd.szFullFile,s); strcat(Fd.szFullFile,p);
	Freturn=WinFileDlg(HWND_DESKTOP,W->handle(),&Fd);
	if (!Freturn || Fd.lReturn!=DID_OK) return 0;
	return Fd.szFullFile;
}

