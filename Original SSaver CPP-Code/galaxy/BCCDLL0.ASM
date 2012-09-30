.386
_TEXT	segment dword use32 public 'CODE'	;size is 20
_TEXT	ends
_DATA	segment dword use32 public 'DATA'	;size is 0
	__os2hmod	dd	0
_DATA	ends
FLAT	group	
	public	__os2hmod
	extrn	_DLL_InitTerm

;	public	_text
_TEXT	segment
MODEL OS2 FLAT
.startup
_textstart:
		mov	eax, 4[esp]
		mov	[__os2hmod], eax
		mov	eax, 1
		ret
_TEXT	ends
	end
