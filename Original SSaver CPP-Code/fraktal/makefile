#
#	sample saver module makefile
#
#
# $$$$$ replace MODULENAME below with the file name of your module $$$$$
#
MODULENAME=sample

all:	$(MODULENAME).ssm

$(MODULENAME).res:	$(MODULENAME).rc $(MODULENAME).dlg $(MODULENAME).h ssaver.ico
	rc -r -p -x $(MODULENAME).rc

$(MODULENAME).ssm:	$(MODULENAME).c $(MODULENAME).h $(MODULENAME).res $(MODULENAME).def
	gcc -s -O2 -Wall -ZC++-comments -Zmtd -Zomf -Zdll $(MODULENAME).c -o $(MODULENAME).ssm $(MODULENAME).def $(MODULENAME).res

	copy $(MODULENAME).ssm e:\wpsutil\ssaver
