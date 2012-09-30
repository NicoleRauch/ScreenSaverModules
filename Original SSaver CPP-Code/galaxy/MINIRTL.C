/*
	minirtl.c
	mini run time library for use with
	ScreenSaver modules with Borland C++
*/
#define INCL_DOS
#define INCL_WIN
#include <os2.h>
#include <string.h>
#include <malloc.h>
#include <memory.h>
#include <process.h>

/*
	this part of the file was taken from emx 0.8g
	-- Copyright (c) 1990-1993 by Eberhard Mattes
*/

void	*memcpy (void *s1, const void *s2, size_t n)
{
	size_t i;

	for (i = 0; i < n; ++i)
		((char *)s1)[i] = ((char *)s2)[i];
	return (s1);
}
void	*memset (void *s, int c, size_t n)
{
	size_t i;
	for (i = 0; i < n; ++i)
	    ((char *)s)[i] = (char)c;
	return (s);
}
char	*strcat (char *string1, const char *string2)
{
	char *dst;
	dst = string1;
	while (*dst != 0)
		++dst;
	while ((*dst = *string2) != 0)
		++dst, ++string2;
	return (string1);
}
char	*strcpy (char *string1, const char *string2)
{
	char *dst;
	dst = string1;
	while ((*dst = *string2) != 0)
		++dst, ++string2;
	return (string1);
}
size_t	strlen (const char *string)
{
	size_t i;
	i = 0;
	while (string[i] != 0) ++i;
	return (i);
}

static unsigned int __rand = 1;
int	rand()
{
	__rand = __rand * 69069 + 5;
	return ((__rand >> 16) & 0x7fff);
}
void	srand(unsigned int seed)
{
	__rand = seed;
}

/*
	the remainder of the file is
	(C) 1993-94 Siegfried Hanisch
*/

// if 1MB is not enough, increase this value
// the memory is not commited, so it does not use physical memory
// until it is accessed
#define MEMPOOLSIZE		(1024L*1024L)
static	void	*mempool = NULL;

void	*calloc(size_t elements, size_t size)
{
	return malloc(elements*size);
}

void	*malloc(size_t size)
{
	APIRET	rc;
	size_t	*mem;
	rc = DosSubAllocMem(mempool, (PVOID *)&mem, size+sizeof(size_t));
	if(rc){
		WinAlarm(HWND_DESKTOP, WA_ERROR);
		return NULL;
	}
	mem[0] = size;
	return (void *)(&mem[1]);
}

void	free(void *mem)
{
	APIRET	rc;
	size_t	*m;
	m = (size_t *)mem;
	rc = DosSubFreeMem(mempool, &m[-1], m[-1]+sizeof(size_t));
	if(rc){
		WinAlarm(HWND_DESKTOP, WA_ERROR);
	}
}

void	*realloc(void *mem, size_t size)
{
	void	*new_mem;
	size_t	*m;
	size_t	old_size;
	m = (size_t *)mem;
	old_size = m[-1];

	new_mem = malloc(size);
	memcpy(new_mem, mem, old_size);
	memset(((char *)new_mem)+old_size, 0 , size-old_size);
	free(mem);
	return new_mem;
}

char	*strdup(const char *string)
{
	return strcpy(malloc(strlen(string)+1), string);
}

BOOL	alloc_heap()
{
	APIRET	rc;
	rc = DosAllocMem((PVOID *)&mempool, MEMPOOLSIZE, PAG_READ|PAG_WRITE);
	if(rc){
		WinAlarm(HWND_DESKTOP, WA_ERROR);
		mempool = NULL;
		return FALSE;
	}

	rc = DosSubSetMem(mempool, DOSSUB_INIT|DOSSUB_SPARSE_OBJ, MEMPOOLSIZE);
	if(rc){
		DosFreeMem(mempool);
		WinAlarm(HWND_DESKTOP, WA_ERROR);
		return FALSE;
	}
	return TRUE;
}

void	free_heap()
{
	APIRET	rc;
	rc = DosSubUnsetMem(mempool);
	if(rc){
		WinAlarm(HWND_DESKTOP, WA_ERROR);
	}
	rc = DosFreeMem(mempool);
	if(rc){
		WinAlarm(HWND_DESKTOP, WA_ERROR);
	}
}

#if defined(__BORLANDC__)
int _beginthread(void (_USERENTRY *__start)(void *), unsigned __stksize, void *__arg)
{
	TID	tid;
	if(DosCreateThread(&tid, (PFNTHREAD)__start, (ULONG)__arg, 2L, __stksize) != 0)
		return -1;
	return tid;
}

void	_endthread(void)
{
	DosExit(EXIT_THREAD, 0);
}
#elif defined(__IBMC__)
int	_Optlink beginthread( void(* _Optlink __thread)(void *),
  void *stack, unsigned stack_size, void *arg_list);
{
	TID	tid;
	if(DosCreateThread(&tid, (PFNTHREAD)__thread, (ULONG)arg_list, 2L, stack_size) != 0)
		return -1;
	return tid;
}

void	_endthread()
{
	DosExit(EXIT_THREAD, 0);
}
#elif defined(__EMX__)
int	_beginthread(void (*start)(void *arg), void *stack,
  unsigned stack_size, void *arg_list)
{
	TID	tid;
	if(DosCreateThread(&tid, (PFNTHREAD)start, (ULONG)arg_list, 2L, stack_size) != 0)
		return -1;
	return tid;
}

void	_endthread()
{
	DosExit(EXIT_THREAD, 0);
}
#endif
