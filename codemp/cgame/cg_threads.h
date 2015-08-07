////////////////////////////////////////////////////////////////////////////////
// 
// $LastChangedBy: ensiform $
// $LastChangedDate: 2007-08-20 22:41:06 -0500 (Mon, 20 Aug 2007) $
// $LastChangedRevision: 108 $
// $Filename: cg_threads.h $
//
////////////////////////////////////////////////////////////////////////////////

#ifndef _CG_THREADS_H
#define _CG_THREADS_H

void CG_InitThreads(void);

int create_thread(void *(*thread_function)(void *),void *arguments);

#endif
