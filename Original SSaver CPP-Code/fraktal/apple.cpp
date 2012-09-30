#include "easy.h"

#include "apple.h"

String title(IDS_Title);
extern Thread thread;

const double XMIN=-2.2,XMAX=0.8,YMIN=-1.5,YMAX=1.5;
const int ITERATIONS=100;

class MyWindow : public Window
{   double Xmin,Xmax,Ymin,Ymax;
	int Iterations,Active;
	Rectangle R;
	public :
	MyWindow () : Window(ID_Window,title,FCF_NORMAL|FCF_MENU)
	{	Xmin=XMIN; Xmax=XMAX; Ymin=YMIN; Ymax=YMAX;
		Iterations=ITERATIONS; Active=0;
		init();
	}
	virtual void sized ();
	virtual void redraw (PS &ps);
	virtual void clicked (LONG x, LONG y, clicktype click);
	void draw ();
	void activate () { Active=1; thread.start(); }
	double xmin () { return Xmin; }
	double xmax () { return Xmax; }
	double ymin () { return Ymin; }
	double ymax () { return Ymax; }
	int iterations () { return Iterations; }
	void set (double xmin, double xmax, double ymin, double ymax,
		int iterations);
};
MyWindow window;
Menu menu(window);
Help help(window,ID_Help,"APPLE.HLP","");

BitmapPS *bitmap=0;

void MyWindow::set (double xmin, double xmax,
	double ymin, double ymax, int iterations)
{	if (xmin>=xmax) return;
	Xmin=xmin; Xmax=xmax;
	Ymax=(ymax+ymin)/2+(Xmax-Xmin)/2*(double)height()/width();
	Ymin=(ymax+ymin)/2-(Xmax-Xmin)/2*(double)height()/width();
	Iterations=iterations;
}

void copy ()
{	WindowPS ps(window);
	if (!window.visible()) return;
	bitmap->copy(ps);
}

void MyWindow::draw ()
{   int i,j,k,f;
	double zre,zim,wre,wim,wrenew,deltax,deltay;
	ULONG color;
	bitmap->directcolor();
	deltax=(Xmax-Xmin)/width();
	deltay=(Ymax-Ymin)/height();
	for (i=0; i<width(); i++)
	{   zre=Xmin+i*deltax;
		for (j=0; j<=height(); j++)
		{   wre=zre;
			wim=zim=Ymin+j*deltay;
			for (k=0; k<Iterations; k++)
			{	if (wim*wim+wre*wre>16) break;
				wrenew=wre*wre-wim*wim+zre;
				wim=2*wre*wim+zim;
				wre=wrenew;
			}
			if (k<Iterations)
			{   f=255l*k/Iterations;
				color=Rgb(0,255-f,f);
			}
			else color=0;
			bitmap->point(i,j,color);
		}
		if (i%10==0) copy();
	}
	window.update();
}

int backdraw (Parameter p)
{   window.draw();
	return 0;
}

Thread thread(backdraw);

void MyWindow::clicked (LONG x, LONG y, clicktype click)
{   WindowPS ps(window);
	double h;
	double scale=(double)width()/height();
	switch (rubberbox(x,y,click,R,5*scale,5,scale))
	{   case RUBBER_DONE :
			h=Xmin+R.x1()*(Xmax-Xmin)/width();
			Xmax=Xmin+R.x2()*(Xmax-Xmin)/width();
			Xmin=h;
			h=Ymin+R.y1()*(Ymax-Ymin)/height();
			Ymax=Ymin+R.y2()*(Ymax-Ymin)/height();
			Ymin=h;
			thread.start();
			break;
		case RUBBER_START :
			thread.suspend();
			break;
		case RUBBER_CANCEL :
			thread.resume();
			break;
	}
}

void MyWindow::sized ()
{	Ymax=Ymin+(Xmax-Xmin)*height()/width();
	thread.stop();
	if (bitmap) delete bitmap;
	bitmap=new BitmapPS(*this);
	if (Active) thread.start();
}

void MyWindow::redraw (PS &ps)
{	bitmap->copy(ps);
}

void doexit (int command)
{	exit(0);
}

void dohelp (int command)
{	help.general();
}

FileSelector save(window,"*.bmp",1);

void dosave (int command)
{	char *p=save.select();
	if (!p) return;
	bitmap->save(p);
}

class ParamDialog : public Dialog
{	public :
	ParamDialog(Window &window, int id) : Dialog(window,id) {}
	virtual int handler (int command);
};
ParamDialog d(window,ID_Dialog);

DoubleItem
	xmin(ID_Xmin,d,window.xmin()),
	xmax(ID_Xmax,d,window.xmax()),
	ymin(ID_Ymin,d,window.ymin()),
	ymax(ID_Ymax,d,window.ymax());
SpinItem iterations(ID_Iterations,d,
	window.iterations(),10,10000);


int ParamDialog::handler (int command)
{	if (command==ID_All)
	{	xmin.reinit(XMIN); xmax.reinit(XMAX);
		ymin.reinit(YMIN); ymax.reinit(YMAX);
		iterations.reinit(ITERATIONS);
		return 0;	}
	return 1;
}

void parameter (int command)
{	xmin.set(window.xmin()); xmax.set(window.xmax());
	ymin.set(window.ymin()); ymax.set(window.ymax());
	iterations.set(window.iterations());
	d.carryout();	if (d.result()!=DID_OK) return;
	window.set(xmin,xmax,ymin,ymax,iterations);
	window.update();
	thread.start();
}

int main (void)
{   window.size(200,200);
	window.top();
	window.activate();
	menu.add(IDM_Exit,doexit);
	menu.add(IDM_Help,dohelp);
	menu.add(IDM_Parameter,parameter);
	menu.add(IDM_Save,dosave);
	window.loop();
	return 0;
}

