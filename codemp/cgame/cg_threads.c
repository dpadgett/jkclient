////////////////////////////////////////////////////////////////////////////////
// 
// $LastChangedBy: ensiform $
// $LastChangedDate: 2007-08-20 22:41:06 -0500 (Mon, 20 Aug 2007) $
// $LastChangedRevision: 108 $
// $Filename: cg_threads.c $
//
////////////////////////////////////////////////////////////////////////////////

#include "cg_local.h"
#ifdef __linux__
	#include <gnu/lib-names.h>
#else
	#define LIBPTHREAD_SO "/usr/lib/libpthread.dylib"
#endif

#ifndef WIN32
#include <pthread.h>
#include <dlfcn.h>


// tjw: handle for libpthread.so
void *g_pthreads = NULL;

// tjw: pointer to pthread_create() from libpthread.so
static int (*g_pthread_create)
	(pthread_t  *,
	__const pthread_attr_t *,
	void * (*)(void *),
	void *) = NULL;


void CG_InitThreads(void)
{
	if(g_pthreads != NULL) {
		Com_Printf("pthreads already loaded\n");
		return;
	}
	g_pthreads = dlopen(LIBPTHREAD_SO, RTLD_NOW);
	if(g_pthreads == NULL) {
		Com_Printf("could not load libpthread\n%s\n",
			dlerror());
		return;
	}
	Com_Printf("loaded libpthread\n");
	g_pthread_create = dlsym(g_pthreads,"pthread_create");
	if(g_pthread_create == NULL) {
		Com_Printf("could not locate pthread_create\n%s\n",
			dlerror());
		return;
	}
	Com_Printf("found pthread_create\n");
}

int
create_thread(void *(*thread_function)(void *),void *arguments) {
	pthread_t thread_id;

	if(g_pthread_create == NULL) {
		// tjw: pthread_create() returns non 0 for failure
		//      but I don't know what's proper here.
		return -1;
	}
	return g_pthread_create(&thread_id, NULL, thread_function,arguments);
}

#else //WIN32
#include <process.h>

void CG_InitThreads(void)
{
	// forty - we can have thread support in win32 we need to link with the MT runtime and use _beginthread
	//CG_Printf("Threading enabled.\n");
}

int create_thread(void *(*thread_function)(void *),void *arguments) {
	void *(*func)(void *) = /*(void *)*/thread_function;

	//Yay - no complaining
	_beginthread((void ( *)(void *))func, 0, arguments);
	return 0;
}
#endif
