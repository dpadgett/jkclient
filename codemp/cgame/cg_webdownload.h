#ifndef __CGWEBDOWNLOAD__H__
#define __CGWEBDOWNLOAD__H__

int Web_Get(const char *url,const char *referer,const char *name,int resume, int timeout, int (*_progress)(double));

#endif
