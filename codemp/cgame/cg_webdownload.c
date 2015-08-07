#include <curl/curl.h>

#include "cg_webdownload.h"
#include "cg_local.h"

static int Web_Init(void);
static void Web_Cleanup(void);

// to get the size and mime type before the download we can use that
// http://curl.haxx.se/mail/lib-2002-05/0036.html


static CURL *curl=NULL;
static char curl_err[1024];
static int (*progress)(double)=NULL;

static size_t Write(void *ptr, size_t size, size_t nmemb, void *stream)
{
	FILE *f;
	//Com_Printf("name %s path %s\n", data->name, data->path);
	f=fopen((char *)stream,"ab");
	
	if(f==NULL)
		return size*nmemb; // weird
	
	//fwrite(ptr,size,nmemb,(FILE *) stream);
	fwrite(ptr,size,nmemb,f);
	fclose(f);

	return size*nmemb;
}

static int Progress(void *clientp, double dltotal, double dlnow, double ultotal, double ulnow)
{
	// callback
	if(progress!=NULL)
	{
		if(progress(dlnow/dltotal)!=0)
			return 1; // we abort
	}

	//Com_Printf("Progress: %2.2f\n",dlnow*100.0/dltotal);
	return 0;
}

static void Web_Cleanup(void)
{
	if(curl!=NULL) 
	{
		/* always cleanup */
		curl_easy_cleanup(curl);
		curl=NULL;

		// reset callback
		progress=NULL;
	}
}

static int Web_Init(void)
{
	CURLcode code;
	
	if(curl!=NULL)
	{
		Web_Cleanup();
	}

	// reinit
	curl = curl_easy_init();
	// reset callback
	progress=NULL;

	// http://curl.haxx.se/libcurl/c/curl_easy_setopt.html
	/* init some options of curl */
	code=curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, curl_err);
	if( code != CURLE_OK )
	{
		CG_Printf("Failed to set error buffer\n");
		return 0;
	}		

	code=curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0);
	if( code != CURLE_OK )
	{
		CG_Printf("Failed to set NoProgress\n");
		return 0;
	}

	code=curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1 );
	if( code != CURLE_OK )
	{
		CG_Printf("Failed to set libcurl nosignal mode\n");
		return 0;
	}		

	code=curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1);
	if( code != CURLE_OK )
	{
		CG_Printf("Failed to set FollowLocation\n");
		return 0;
	}

	code=curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 2);
	if( code != CURLE_OK )
	{
		CG_Printf("Failed to set Max Redirection\n");
		return 0;
	}

	code=curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, Write);
	if( code != CURLE_OK )
	{
		CG_Printf("Failed to set writer callback function\n");
		return 0;
	}		

	code=curl_easy_setopt(curl, CURLOPT_PROGRESSFUNCTION, Progress);
	if( code != CURLE_OK )
	{
		CG_Printf("Failed to set progress callback function\n");
		return 0;
	}		

	code=curl_easy_setopt(curl, CURLOPT_USERAGENT, "TEHs Clientmod 2.0");
	if( code != CURLE_OK )
	{
		CG_Printf("Failed to set UserAgent\n");
		return 0;
	}

	code=curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1);
	if( code != CURLE_OK )
	{
		CG_Printf("Failed to set fail on error\n");
		return 0;
	}		

	return 1;
}

// url needs a / at the end
int Web_Get(const char *url, const char *referer, const char *name, int resume, int timeout, int (*_progress)(double))
{
	CURLcode code;
	FILE *f;
	unsigned int fsize;

	// init/reinit curl
	Web_Init();

	code=curl_easy_setopt(curl, CURLOPT_URL, url);
	if( code != CURLE_OK )
	{
		CG_Printf("Failed to set url\n");
		return 0;
	}		

	if( referer )
	{
		code=curl_easy_setopt(curl, CURLOPT_REFERER, referer);
		if( code != CURLE_OK )
		{
			CG_Printf("Failed to set Referer\n");
			return 0;
		}
	}

	// connection timeout
	code=curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, timeout );
	if( code != CURLE_OK )
	{
		CG_Printf("Failed to set libcurl connection timeout\n");
		return 0;
	}

	code=curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeout );
	if( code != CURLE_OK )
	{
		CG_Printf("Failed to set libcurl global timeout\n");
		return 0;
	}

	if(resume==1)
	{
		// test if file exist
		if((f=fopen(name,"r"))==NULL)
		{
			// file does not exist
			goto new_file;
		}
		// the file exist
		// get the size
		fsize=fseek(f,0,SEEK_END);
		fclose(f);

		code=curl_easy_setopt(curl, CURLOPT_RESUME_FROM, fsize); 
		if( code != CURLE_OK )
		{
			CG_Printf("Failed to set file resume from length\n");
			return 0;
		}		

		// file exist all good
	}

new_file:

	code=curl_easy_setopt(curl, CURLOPT_FILE, name); 
	//code=curl_easy_setopt(curl, CURLOPT_FILE, &f); 
	if( code != CURLE_OK )
	{
		CG_Printf("Failed to set writer data\n");
		return 0;
	}		

	CG_Printf("Downloading %s from %s\n",name,url);

	// set callback
	progress=_progress;
	code=curl_easy_perform(curl);
	if( code != CURLE_OK )
	{
		CG_Printf("Failed to download %s from %s\n",name,url);
		CG_Printf("Error: %s\n",curl_err);

		Web_Cleanup();
		return 0;
	}

	Web_Cleanup();
	return 1;
}

void *plz2download(void *args) {
	int success;
	cg_downloadInfo_t *dlInfo = (cg_downloadInfo_t *)args;

	success = Web_Get(dlInfo->remoteurl, NULL, dlInfo->localfile, 0, 0, NULL);
	return 0;
}
