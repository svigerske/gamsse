/* Satalia SolveEngine link code
 *
 * Links:
 * - https://curl.haxx.se/libcurl/c/libcurl.html
 * - https://github.com/DaveGamble/cJSON/tree/v1.5.3
 * - http://libb64.sourceforge.net/
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>  /* for sleep() */
#include <time.h>  /* for strptime */

#ifdef _WIN32
#define timegm _mkgmtime
#endif

#include "curl/curl.h"
#include "cJSON.h"
#include "base64encode.h"

#include "gamsse.h"
#include "gmomcc.h"
#include "gevmcc.h"
#include "optcc.h"

#include "convert.h"

typedef struct
{
   size_t  size;
   size_t  length;
   void*   content;
} buffer_t;

#define BUFFERINIT {0,0,NULL}

struct gamsse_s
{
   gmoHandle_t gmo;
   gevHandle_t gev;
   optHandle_t opt;
   char*       apikey;
   char*       jobid;
   int         debug;

   CURL*       curl;
   char        curlerrbuf[CURL_ERROR_SIZE];   /**< buffer for curl to store error message */
   struct curl_slist* curlheaders;
   buffer_t    curlwritebuf;

   double      progresslastruntime;
   int         progressisupload;
};

/** struct for writing problem into base64-encoded string */
typedef struct
{
   buffer_t           buffer;
   base64_encodestate es;
} encodeprob_t;

#define CURL_CHECK( se, code ) \
   do \
   { \
      CURLcode curlres; \
      curlres = code; \
      if( curlres != CURLE_OK ) \
      { \
         gevLogStatPChar((se)->gev, "libcurl: "); \
         if( *(se)->curlerrbuf != '\0' ) \
            gevLogStat((se)->gev, (se)->curlerrbuf); \
         else \
            gevLogStat((se)->gev, curl_easy_strerror(curlres)); \
         goto TERMINATE; \
      } \
   } while( 0 )

static
void exitbuffer(
   buffer_t* buf
   )
{
   free(buf->content);
   buf->content = NULL;
   buf->size = 0;
   buf->length = 0;
}

/** Ensures that there is space for at least size many additional characters in the buffer.
 *
 * @return Remaining space in buffer. Returns 0 if realloc failed.
 */
static
size_t ensurebuffer(
   buffer_t* buffer,
   size_t    size
   )
{
   assert(buffer != NULL);

   if( buffer->size <= buffer->length + size )
   {
      buffer->size = (size_t) (1.2 * (buffer->length + size) + 4);
      buffer->content = realloc(buffer->content, buffer->size);

      assert(buffer->content != NULL);
      if( buffer->content == NULL )
      {
         buffer->size = 0;
         buffer->length = 0;
      }
   }

   return buffer->size - buffer->length;
}

static
size_t appendbuffer(
   buffer_t* buffer,
   char*     msg
)
{
   size_t msglen;

   assert(buffer != NULL);
   assert(msg != NULL);

   msglen = strlen(msg);

   if( ensurebuffer(buffer, msglen+1) < msglen + 1 )
      return 0; /* there was a problem in increasing the buffer */

   memcpy(buffer->content + buffer->length, msg, msglen+1);  /* include closing '\0', but do not count it into length, so it gets overwritten next */
   buffer->length += msglen;

   return msglen;
}

static
size_t appendbufferCurl(
   void*  curlbuf,
   size_t size,
   size_t nmemb,
   void*  buf)
{
   buffer_t* buffer = (buffer_t*)buf;

   assert(curlbuf != NULL || nmemb == 0);
   assert(buffer != NULL);

   if( ensurebuffer(buffer, nmemb * size) < nmemb * size )
      return 0; /* there was a problem in increasing the buffer */

   memcpy(buffer->content + buffer->length, curlbuf, nmemb * size);
   buffer->length += nmemb * size;

   return nmemb;
}

/** appendbuffer function for use in convert */
static
DECL_convertWriteFunc(appendbufferConvert)
{
   encodeprob_t* encodeprob;
   size_t msglen;
   int cnt;

   assert(msg != NULL);
   assert(writedata != NULL);

   encodeprob = (encodeprob_t*)writedata;

   msglen = strlen(msg);

   /* need 4/3*msglen many more bytes in buffer */
   if( ensurebuffer(&encodeprob->buffer, (int)(1.5*msglen)+2) < 1.5*msglen )
      return 0;

   cnt = base64_encode_block(msg, msglen, encodeprob->buffer.content + encodeprob->buffer.length, &encodeprob->es);
   encodeprob->buffer.length += cnt;

   return msglen;
}

/* CURLOPT_XFERINFOFUNCTION callback to print progress report */
static int progressreportCurl(
   void*      p,
   curl_off_t dltotal,
   curl_off_t dlnow,
   curl_off_t ultotal,
   curl_off_t ulnow
   )
{
   gamsse_t* se = (gamsse_t*) p;
   double curtime = 0.0;
   char buf[GMS_SSSIZE];

   assert(se!= NULL);

   CURL_CHECK( se, curl_easy_getinfo(se->curl, CURLINFO_TOTAL_TIME, &curtime) );

   /* don't print if less than 1 second passed since last print */
   if( (curtime - se->progresslastruntime) < 1.0 )
      return 0;
   se->progresslastruntime = curtime;

   sprintf(buf, "%6.1fs: %" CURL_FORMAT_CURL_OFF_T " of %" CURL_FORMAT_CURL_OFF_T " bytes %s\n", curtime,
      se->progressisupload ? ulnow : dlnow, se->progressisupload ? ultotal : dltotal, se->progressisupload ? "uploaded" : "downloaded");
   gevLogPChar(se->gev, buf);

   /* stop curl if user interrupt */
   if( gevTerminateGet(se->gev) )
   {
      gevLog(se->gev, "User interrupt.");
      gmoSolveStatSet(se->gmo, gmoSolveStat_User);
      return 1;
   }

TERMINATE:
   return 0;
}

static
char* formattime(
   char*  buf,     /**< buffer to store time, must be at least 26 chars */
   char*  src      /**< time string to convert, should be in form "2017-06-20T10:25:10Z" */
   )
{
   time_t seconds;
   struct tm tm;

   if( src == NULL )
   {
      strcpy(buf, "N/A");
      return buf;
   }

   if( strlen(src) != 20 )
   {
      strcpy(buf, "N/A");
      return buf;
   }

   /* convert UTC timestring into struct tm */
   strptime(src, "%Y-%m-%dT%H:%M:%SZ", &tm);

   /* convert struct tm into seconds since epoch */
   /* on Windows, this might be _mkgmtime, https://msdn.microsoft.com/en-us/library/2093ets1.aspx */
   seconds = timegm(&tm);

   /* convert seconds since epoch into time string in local time zone */
   if( ctime_r(&seconds, buf) == NULL )
   {
      sprintf(buf, "N/A");
      return buf;
   }

   /* remove trailing \n */
   buf[strlen(buf)-1] = '\0';

   return buf;
}

static
RETURN initCurl(
   gamsse_t* se
)
{
   RETURN rc = RETURN_ERROR;
   char buffer[100];

   assert(se->apikey != NULL);
   assert(se->curl == NULL);
   assert(se->curlheaders == NULL);

   /* create curl easy handle */
   se->curl = curl_easy_init();
   if( se->curl == NULL )
   {
      gevLogStat(se->gev, "Error in curl_easy_init()\n");
      goto TERMINATE;
   }

   /* create curl http header for authorization */
   snprintf(buffer, sizeof(buffer), "Authorization: api-key %s", se->apikey);
   se->curlheaders = curl_slist_append(NULL, buffer);
   assert(se->curlheaders != NULL);

   rc = RETURN_OK;
TERMINATE:
   return rc;
}

static
RETURN resetCurl(
   gamsse_t* se
)
{
   RETURN rc = RETURN_ERROR;

   assert(se != NULL);
   assert(se->curl != NULL);
   assert(se->curlheaders != NULL);

   /* reset all options */
   curl_easy_reset(se->curl);

   /* set error buffer */
   *se->curlerrbuf = '\0';
   CURL_CHECK( se, curl_easy_setopt(se->curl, CURLOPT_ERRORBUFFER, se->curlerrbuf) );

   /* set write buffer */
   se->curlwritebuf.length = 0;
   CURL_CHECK( se, curl_easy_setopt(se->curl, CURLOPT_WRITEFUNCTION, appendbufferCurl) );
   CURL_CHECK( se, curl_easy_setopt(se->curl, CURLOPT_WRITEDATA, &se->curlwritebuf) );

   /* set http header (api key) */
   CURL_CHECK( se, curl_easy_setopt(se->curl, CURLOPT_HTTPHEADER, se->curlheaders) );

   if( se->debug >= 2 )
   {
      /* enable curl verbose output */
      CURL_CHECK( se, curl_easy_setopt(se->curl, CURLOPT_VERBOSE, 1) );
   }

   rc = RETURN_OK;
TERMINATE:
   return rc;
}

static
RETURN performCurl(
   gamsse_t* se,
   cJSON**   json
)
{
   RETURN rc = RETURN_ERROR;
   long respcode;

   if( json != NULL )
      *json = NULL;

   /* perform HTTP request */
   CURL_CHECK( se, curl_easy_perform(se->curl) );

   if( se->debug )
   {
      char* url = NULL;
      CURL_CHECK( se, curl_easy_getinfo(se->curl, CURLINFO_EFFECTIVE_URL, &url) );
      if( url != NULL )
      {
         gevLogPChar(se->gev, "DEBUG Connected to ");
         gevLogPChar(se->gev, url);
         gevLogPChar(se->gev, "\n");
      }
   }

   if( se->curlwritebuf.content == NULL )  /* got no output at all */
   {
      gevLogStat(se->gev, "Failure in connection from SolveEngine: Response is empty.");
      goto TERMINATE;
   }

   /* add terminating \0 */
   ((char*)se->curlwritebuf.content)[se->curlwritebuf.length] = '\0';

   if( se->debug )
   {
      gevLogPChar(se->gev, "DEBUG Answer from SolveEngine: ");
      gevLogPChar(se->gev, (char*)se->curlwritebuf.content);
      gevLogPChar(se->gev, "\n");
   }

   /* check HTTP response code */
   CURL_CHECK( se, curl_easy_getinfo(se->curl, CURLINFO_RESPONSE_CODE, &respcode) );
   if( se->debug )
   {
      char buffer[GMS_SSSIZE];
      sprintf(buffer, "DEBUG HTTP response code: %ld\n", respcode);
      gevLogPChar(se->gev, buffer);
   }

   if( respcode >= 400 )
   {
      gevLogStat(se->gev, "Failure from SolveEngine:");
      gevLogStatPChar(se->gev, se->curlwritebuf.content);
      goto TERMINATE;
   }

   if( json != NULL )
   {
      /* parse response */
      *json = cJSON_Parse((char*)se->curlwritebuf.content);
      if( *json == NULL )
      {
         gevLogStatPChar(se->gev, "Failure parsing SolveEngine response. Content: ");
         gevLogStatPChar(se->gev, se->curlwritebuf.content);
         gevLogStatPChar(se->gev, "\n");
         goto TERMINATE;
      }
   }

   rc = RETURN_OK;
TERMINATE:
   return rc;
}


static
void printjoblist(
   gamsse_t* se
)
{
   gevHandle_t gev = se->gev;
   char strbuffer[1024];
   cJSON* root = NULL;
   cJSON* jobs = NULL;
   cJSON* total = NULL;
   int j;

   if( resetCurl(se) != RETURN_OK )
      goto TERMINATE;

   /* set URL */
   CURL_CHECK( se, curl_easy_setopt(se->curl, CURLOPT_URL, "https://solve.satalia.com/api/v2/jobs?per_page=2147483647") );

   /* perform HTTP request */
   if( performCurl(se, &root) != RETURN_OK )
      goto TERMINATE;
   assert(root != NULL);

   total = cJSON_GetObjectItem(root, "total");
   if( total == NULL || !cJSON_IsNumber(total) )
      gevLogPChar(gev, "Printing Joblist:\n");
   else
   {
      sprintf(strbuffer, "Printing Joblist: %d jobs in total\n", total->valueint);
      gevLogPChar(gev, strbuffer);

      if( total->valueint == 0 )
         goto TERMINATE;
   }

   jobs = cJSON_GetObjectItem(root, "jobs");
   if( jobs == NULL || !cJSON_IsArray(jobs) )
   {
      gevLogStat(gev, "printjoblist: No 'jobs' found in answer from SolveEngine.");
      goto TERMINATE;
   }

   sprintf(strbuffer, "%40s %5s %10s %30s %30s %30s Used\n",
      "Job ID", "Algo", "Status", "Submittime", "Starttime", "Finishtime");
   gevLogPChar(gev, strbuffer);

   for( j = 0; j < cJSON_GetArraySize(jobs); ++j )
   {
      cJSON* job;
      cJSON* id;
      cJSON* status;
      cJSON* algo;
      cJSON* submitted;
      cJSON* started;
      cJSON* finished;
      cJSON* usedtime;
      char submittedbuf[32];
      char startedbuf[32];
      char finishedbuf[32];
      job = cJSON_GetArrayItem(jobs, j);
      assert(job != NULL);

      id = cJSON_GetObjectItem(job, "id");
      if( id == NULL )
         continue;

      status = cJSON_GetObjectItem(job, "status");
      algo = cJSON_GetObjectItem(job, "algorithm");
      status = cJSON_GetObjectItem(job, "status");
      submitted = cJSON_GetObjectItem(job, "submitted");
      started = cJSON_GetObjectItem(job, "started");
      finished = cJSON_GetObjectItem(job, "finished");
      usedtime = cJSON_GetObjectItem(job, "used_time");

      sprintf(strbuffer, "%s %5s %10s %30s %30s %30s %4d\n",
         id->valuestring,
         algo != NULL ? algo->valuestring : "N/A",
         status != NULL ? status->valuestring : "N/A",
         formattime(submittedbuf, submitted != NULL ? submitted->valuestring : NULL),
         formattime(startedbuf, started != NULL ? started->valuestring : NULL),
         formattime(finishedbuf, finished != NULL ? finished->valuestring : NULL),
         usedtime ? usedtime->valueint : 0
      );
      gevLogPChar(gev, strbuffer);
   }

TERMINATE :
   if( root != NULL )
      cJSON_Delete(root);
}

/* returns job id */
static
RETURN submitjob(
   gamsse_t* se
   )
{
   gevHandle_t gev = se->gev;
   gmoHandle_t gmo = se->gmo;
   cJSON* root = NULL;
   cJSON* id = NULL;
   char* postfields = NULL;
   encodeprob_t encodeprob = { .buffer = BUFFERINIT };
   RETURN rc = RETURN_ERROR;

   assert(se->jobid == NULL);

   gevLog(gev, "Submitting Job.");

   if( resetCurl(se) != RETURN_OK )
      goto TERMINATE;

   CURL_CHECK( se, curl_easy_setopt(se->curl, CURLOPT_URL, "https://solve.satalia.com/api/v2/jobs") );

   /* post fields */
   appendbuffer(&encodeprob.buffer, "{\"options\":{},\"problems\":[{\"name\":\"problem.lp\",\"data\": \"");
   assert(encodeprob.buffer.length > 0);

   /* append base64 encode of string in LP format (this will not be 0-terminated) */
   base64_init_encodestate(&encodeprob.es);
   if( writeLP(gmo, gev, appendbufferConvert, &encodeprob) != RETURN_OK )
   {
      gevLogStat(gev, "submitjob: Error converting problem.\n");
      goto TERMINATE;
   }
   if( ensurebuffer(&encodeprob.buffer, 6) < 6 )  /* 2 for blockend, 4 for terminating "}]} */
   {
      gevLogStat(gev, "submitjob: Out-of-memory converting problem.\n");
      goto TERMINATE;
   }
   encodeprob.buffer.length += base64_encode_blockend(encodeprob.buffer.content + encodeprob.buffer.length, &encodeprob.es);
   appendbuffer(&encodeprob.buffer, "\"}]}");
   CURL_CHECK( se, curl_easy_setopt(se->curl, CURLOPT_POSTFIELDS, encodeprob.buffer.content) );

   /* get a progress report since this can take time for larger problems */
   se->progresslastruntime = 0;
   se->progressisupload = 1;
   CURL_CHECK( se, curl_easy_setopt(se->curl, CURLOPT_XFERINFOFUNCTION, progressreportCurl) );
   CURL_CHECK( se, curl_easy_setopt(se->curl, CURLOPT_XFERINFODATA, se) );
   CURL_CHECK( se, curl_easy_setopt(se->curl, CURLOPT_NOPROGRESS, 0L) );

   /* perform HTTP request */
   if( performCurl(se, &root) != RETURN_OK )
      goto TERMINATE;
   assert(root != NULL);

   id = cJSON_GetObjectItem(root, "id");
   if( id == NULL || !cJSON_IsString(id) )
   {
      gevLogStat(gev, "submitjob: Failure obtaining job id from SolveEngine.");
      goto TERMINATE;
   }

   se->jobid = strdup(id->valuestring);
   rc = RETURN_OK;

TERMINATE :
   if( root != NULL )
      cJSON_Delete(root);

   if( postfields != NULL )
      postfields = NULL;

   exitbuffer(&encodeprob.buffer);

   return rc;
}

/* schedule a job to be run */
static
RETURN schedulejob(
   gamsse_t* se
   )
{
   gevHandle_t gev = se->gev;
   char strbuffer[1024];
   int rc = RETURN_ERROR;

   gevLogPChar(gev, "Scheduling Job. ID: "); gevLog(gev, se->jobid);

   if( resetCurl(se) != RETURN_OK )
      goto TERMINATE;

   assert(se->jobid != NULL);
   sprintf(strbuffer, "https://solve.satalia.com/api/v2/jobs/%s/schedule", se->jobid);
   CURL_CHECK( se, curl_easy_setopt(se->curl, CURLOPT_URL, strbuffer) );

   /* we want an empty POST request */
   CURL_CHECK( se, curl_easy_setopt(se->curl, CURLOPT_POSTFIELDS, "") );

   /* perform HTTP request */
   if( performCurl(se, NULL) != RETURN_OK )
      goto TERMINATE;

   if( se->curlwritebuf.length > 2 )
   {
      /* something else went wrong */
      gevLogStatPChar(gev, "schedulejob: Failed to schedule job: ");
      gevLogStatPChar(gev, (char*)se->curlwritebuf.content);
      gevLogStatPChar(gev, "\n");
   }

   rc = RETURN_OK;
TERMINATE:
   return rc;
}

/* job status */
static
char* jobstatus(
   gamsse_t* se
   )
{
   gevHandle_t gev = se->gev;
   char strbuffer[1024];
   cJSON* root = NULL;
   cJSON* status = NULL;
   char* statusstr = NULL;

   if( resetCurl(se) != RETURN_OK )
      goto TERMINATE;

   assert(se->jobid != NULL);
   sprintf(strbuffer, "https://solve.satalia.com/api/v2/jobs/%s/status", se->jobid);
   CURL_CHECK( se, curl_easy_setopt(se->curl, CURLOPT_URL, strbuffer) );

   /* perform HTTP request */
   if( performCurl(se, &root) != RETURN_OK )
      goto TERMINATE;

   status = cJSON_GetObjectItem(root, "status");
   if( status == NULL || !cJSON_IsString(status) )
   {
      gevLogStat(gev, "jobstatus: No 'status' in answer from SolveEngine.");
      goto TERMINATE;
   }

   statusstr = strdup(status->valuestring);

TERMINATE :
   if( root != NULL )
      cJSON_Delete(root);

   return statusstr;
}

/* solution */
static
void getsolution(
   gamsse_t* se
   )
{
   gevHandle_t gev = se->gev;
   gmoHandle_t gmo = se->gmo;
   char strbuffer[1024];
   cJSON* root = NULL;
   cJSON* results = NULL;
   cJSON* status = NULL;
   cJSON* objval = NULL;
   cJSON* variables = NULL;

   gevLog(gev, "Retrieving results.");

   if( resetCurl(se) != RETURN_OK )
      goto TERMINATE;

   assert(se->jobid != NULL);
   sprintf(strbuffer, "https://solve.satalia.com/api/v2/jobs/%s/results", se->jobid);
   CURL_CHECK( se, curl_easy_setopt(se->curl, CURLOPT_URL, strbuffer) );

   /* get a progress report since this can take time for larger problems */
   se->progresslastruntime = 0;
   se->progressisupload = 0;
   CURL_CHECK( se, curl_easy_setopt(se->curl, CURLOPT_XFERINFOFUNCTION, progressreportCurl) );
   CURL_CHECK( se, curl_easy_setopt(se->curl, CURLOPT_XFERINFODATA, se) );
   CURL_CHECK( se, curl_easy_setopt(se->curl, CURLOPT_NOPROGRESS, 0L) );

   /* perform HTTP request */
   if( performCurl(se, &root) != RETURN_OK )
      goto TERMINATE;

   results = cJSON_GetObjectItem(root, "result");
   if( results == NULL )
   {
      gevLogStat(gev, "getsolution: No 'result' in solution from SolveEngine");
      goto TERMINATE;
   }

   status = cJSON_GetObjectItem(results, "status");
   if( status == NULL || !cJSON_IsString(status) )
   {
      gevLogStat(gev, "getsolution: No 'status' in solution from SolveEngine");
      goto TERMINATE;
   }

   gevLogStatPChar(gev, "Status: ");
   gevLogStat(gev, status->valuestring);

   objval = cJSON_GetObjectItem(results, "objective_value");
   if( objval != NULL && cJSON_IsNumber(objval) )
   {
      sprintf(strbuffer, "Objective Value: %.10e\n", objval->valuedouble);
      gevLogStatPChar(gev, strbuffer);
   }

   if( strcmp(status->valuestring, "optimal") == 0 )
   {
      cJSON* varvalpair;
      cJSON* varname;
      cJSON* val;
      int i;
      long int varidx;
      char* endptr;
      char namebuf[GMS_SSSIZE];

      variables = cJSON_GetObjectItem(results, "variables");
      if( variables == NULL || !cJSON_IsArray(variables) )
      {
         gevLogStat(gev, "getsolution: No 'variables' array in optimal solution report from SolveEngine.\n");
         goto TERMINATE;
      }

      if( cJSON_GetArraySize(variables) != gmoN(gmo) )
      {
         sprintf(strbuffer, "Number of variables in solution (%d) does not match GAMS instance (%d)\n", cJSON_GetArraySize(variables), gmoN(gmo));
         gevLogStat(gev, strbuffer);
         goto TERMINATE;
      }

      for( i = 0; i < cJSON_GetArraySize(variables); ++i)
      {
         varvalpair = cJSON_GetArrayItem(variables, i);
         assert(varvalpair != NULL);

         varname = cJSON_GetObjectItem(varvalpair, "name");
         if( varname == NULL || !cJSON_IsString(varname) || strlen(varname->valuestring) < 2 )
         {
            gevLogStat(gev, "getsolution: No 'name' in variable result.\n");
            goto TERMINATE;
         }

         val = cJSON_GetObjectItem(varvalpair, "value");
         if( val == NULL || !cJSON_IsNumber(val) )
         {
            gevLogStat(gev, "getsolution: No 'value' in variable result.\n");
            goto TERMINATE;
         }

         varidx = strtol(varname->valuestring+1, &endptr, 10);
         if( *endptr != '\0' || varidx < 0 || varidx > gmoN(gmo) )
         {
            gevLogPChar(gev, "Error parsing variable result ");
            gevLog(gev, cJSON_Print(varvalpair));
         }

         gmoSetVarLOne(gmo, varidx, val->valuedouble);

         if( gmoN(gmo) < 30 )
         {
            sprintf(strbuffer, "%s = %g\n", gmoGetVarNameOne(gmo, varidx, namebuf), val->valuedouble);
            gevLogPChar(gev, strbuffer);
         }
      }

      gmoModelStatSet(gmo, gmoModelStat_OptimalGlobal);
      gmoSolveStatSet(gmo, gmoSolveStat_Normal);

      gmoSetHeadnTail(gmo, gmoHmarginals, 0);
      gmoCompleteSolution(gmo);
   }
   else
   {
      /* TODO what other status could SolveEngine return? (e.g., infeasible) */
      gmoModelStatSet(gmo, gmoModelStat_NoSolutionReturned);
      gmoSolveStatSet(gmo, gmoSolveStat_Normal);
   }

TERMINATE :
   if( root != NULL )
      cJSON_Delete(root);
}

/* stop a started job */
static
void stopjob(
   gamsse_t* se
   )
{
   gevHandle_t gev = se->gev;
   char strbuffer[1024];

   /* gevLog(gev, "Stop job"); */

   if( resetCurl(se) != RETURN_OK )
      goto TERMINATE;

   assert(se->jobid != NULL);
   sprintf(strbuffer, "https://solve.satalia.com/api/v2/jobs/%s/stop", se->jobid);
   CURL_CHECK( se, curl_easy_setopt(se->curl, CURLOPT_URL, strbuffer) );

   CURL_CHECK( se, curl_easy_setopt(se->curl, CURLOPT_CUSTOMREQUEST, "DELETE") );

   /* perform HTTP request */
   if( performCurl(se, NULL) != RETURN_OK )
      goto TERMINATE;

   if( se->curlwritebuf.length > 2 )
   {
      /* something else went wrong */
      gevLogStatPChar(gev, "stopjob: Failure stopping job: ");
      gevLogStatPChar(gev, (char*)se->curlwritebuf.content);
      gevLogStatPChar(gev, "\n");
   }

TERMINATE : ;
}

/* stop a started job, doesn't seem to delete the job */
static
void deletejob(
   gamsse_t* se
   )
{
   gevHandle_t gev = se->gev;
   char strbuffer[1024];

   /* gevLog(gev, "Deleting job"); */

   if( resetCurl(se) != RETURN_OK )
      goto TERMINATE;

   assert(se->jobid != NULL);
   sprintf(strbuffer, "https://solve.satalia.com/api/v2/jobs/%s", se->jobid);
   CURL_CHECK( se, curl_easy_setopt(se->curl, CURLOPT_URL, strbuffer) );

   CURL_CHECK( se, curl_easy_setopt(se->curl, CURLOPT_CUSTOMREQUEST, "DELETE") );

   /* perform HTTP request */
   if( performCurl(se, NULL) != RETURN_OK )
      goto TERMINATE;

   if( se->curlwritebuf.length > 2 )
   {
      /* something else went wrong */
      gevLogStatPChar(gev, "deletejob: Failure deleting job: ");
      gevLogStatPChar(gev, (char*)se->curlwritebuf.content);
      gevLogStatPChar(gev, "\n");
   }

TERMINATE : ;
}

static
int dooptions(
   gamsse_t*   se
)
{
   gevHandle_t gev = se->gev;
   optHandle_t opt;
   char buffer[GMS_SSSIZE+30];
   int ival;
   int i;

   if( !optGetReady(buffer, sizeof(buffer)) )
   {
      gevLogStat(gev, buffer);
      return 1;
   }

   if( !optCreate(&se->opt, buffer, sizeof(buffer)) )
   {
      gevLogStat(gev, buffer);
      return 1;
   }
   opt = se->opt;
   assert(opt != NULL);

   /* gevGetStrOpt(gev, gevNameSysDir, buffer); */ *buffer = '\0';
   strcat(buffer, "optsolveengine.def");

   if( optReadDefinition(opt, buffer) )
   {
      gevStatCon(gev);
      for( i = 1; i <= optMessageCount(opt); ++i )
      {
         optGetMessage(opt, i, buffer, &ival);
         gevLogStat(gev, buffer);
      }
      gevStatCoff(gev);
      optClearMessages(opt);
      return 1;
   }
   gevStatCon(gev);
   for( int i = 1; i <= optMessageCount(opt); ++i)
   {
      optGetMessage(opt, i, buffer, &ival);
      gevLogStat(gev, buffer);
   }
   gevStatCoff(gev);
   optClearMessages(opt);

   /* We don't want the user to quote string options that contain spaces, so we run in EOLonly mode */
   optEOLOnlySet(opt, 1);

   /* initialize options object by getting defaults from options settings file and optionally read user options file */
   if( gmoOptFile(se->gmo) > 0 )
   {
      gmoNameOptFile(se->gmo, buffer);

      optEchoSet(opt, 1);
      optReadParameterFile(opt, buffer);
      /* Echo */
      gevStatCon(gev);
      for( int i = 1; i <= optMessageCount(opt); ++i)
      {
         optGetMessage(opt, i, buffer, &ival);
         if( ival <= optMsgFileLeave || ival == optMsgUserError)
         {
            /* don't echo api key log */
            char* key;
            key = strstr(buffer, "apikey");  /* TODO do case insensitive */
            if( key != NULL )
               strcpy(key+6, " ***secret***");

            gevLogStat(gev, buffer);
         }
      }
      gevStatCoff(gev);
      optClearMessages(opt);
      optEchoSet(opt, 0);
   }

   if( !optGetDefinedStr(opt, "apikey") )
   {
      /* check whether we have an API key for SolveEngine in the environment */
      char* apikey;

      apikey = getenv("SOLVEENGINE_APIKEY");
      if( apikey != NULL )
         optSetStrStr(opt, "apikey", apikey);
   }

   se->debug = optGetIntStr(opt, "debug");

   return 0;
}

void se_Create(
   gamsse_t** se,
   char*      msgBuf,
   int        msgBufLen
)
{
   assert(se != NULL);

   *se = (gamsse_t*) malloc(sizeof(gamsse_t));
}

void se_Free(
   gamsse_t** se
)
{
   free(*se);
   *se = NULL;
}

int se_CallSolver(
   gamsse_t*  se,
   void*      gmo
)
{
   int rc = 1;
   char buffer[1024];
   char* status = NULL;
   double reslim, res;

   if( !gmoGetReady(buffer, sizeof(buffer)) )
   {
      fputs(buffer, stderr);
      return 1;
   }

   if( !gevGetReady(buffer, sizeof(buffer)) )
   {
      fputs(buffer, stderr);
      return 1;
   }

   memset(se, 0, sizeof(gamsse_t));
   se->gmo = gmo;
   se->gev = gmoEnvironment(gmo);

   gevLogStat(se->gev, "This is the GAMS link to Satalia SolveEngine.");

   gmoModelStatSet(se->gmo, gmoModelStat_NoSolutionReturned);
   gmoSolveStatSet(se->gmo, gmoSolveStat_SystemErr);

   if( dooptions(se) )
      goto TERMINATE;

   optGetStrStr(se->opt, "apikey", buffer);
   if( *buffer == '\0' )
   {
      gevLogStat(se->gev, "No SolveEngine API key found in options file (option 'apikey') or environment (SOLVEENGINE_APIKEY). Exiting.");
      goto TERMINATE;
   }
   if( strlen(buffer) > 50 ) /* mine is 45 chars */
   {
      gevLogStat(se->gev, "Invalid API key: too long. Exiting.");
      goto TERMINATE;
   }
   se->apikey = strdup(buffer);

   if( initCurl(se) != RETURN_OK )
      goto TERMINATE;

   if( optGetIntStr(se->opt, "printjoblist") )
      printjoblist(se);

   /* get the problem into a normal form */
   gmoObjStyleSet(se->gmo, gmoObjType_Fun);
   gmoObjReformSet(se->gmo, 1);
   gmoIndexBaseSet(se->gmo, 0);
   gmoSetNRowPerm(se->gmo); /* hide =N= rows */

   if( submitjob(se) != RETURN_OK )
      goto TERMINATE;

   if( schedulejob(se) != RETURN_OK )
      goto TERMINATE;

   reslim = gevGetDblOpt(se->gev, gevResLim);
   gevTimeSetStart(se->gev);

   do
   {
      sleep(1);
      res = gevTimeDiffStart(se->gev);

      free(status);
      status = jobstatus(se);
      sprintf(buffer, "%8.1fs Job Status: %s\n", res, status != NULL ? status : "UNKNOWN");
      gevLogPChar(se->gev, buffer);

      if( gevTerminateGet(se->gev) )
      {
         gevLog(se->gev, "User Interrupt.\n");
         gmoModelStatSet(se->gmo, gmoModelStat_NoSolutionReturned);
         gmoSolveStatSet(se->gmo, gmoSolveStat_User);
         break;
      }

      if( res > reslim )
      {
         gevLog(se->gev, "Time limit reached.\n");
         gmoModelStatSet(se->gmo, gmoModelStat_NoSolutionReturned);
         gmoSolveStatSet(se->gmo, gmoSolveStat_Resource);
         break;
      }
   }
   while( status != NULL && (
     strcmp(status, "queued") == 0 ||   /* if queued, then we wait for available resources - hope that this wouldn't take too long */
     strcmp(status, "translating") == 0 ||
     strcmp(status, "started") == 0 ||
     strcmp(status, "starting") == 0) );

   gmoSetHeadnTail(se->gmo, gmoHresused, gevTimeDiffStart(se->gev));

   /* if job has been completed, then get results */
   if( status != NULL && strcmp(status, "completed") == 0 )
      getsolution(se);

   /* if job has been interrupted (Ctrl+C), then stop it */
   if( status != NULL && (strcmp(status, "started") == 0 || strcmp(status, "starting") == 0) )
      stopjob(se);

   /* if job has failed, then return solver error (instead of system error) */
   if( status != NULL && strcmp(status, "failed") == 0 )
      gmoSolveStatSet(se->gmo, gmoSolveStat_SolverErr);

   rc = 0;
TERMINATE:
   if( se->jobid != NULL && optGetIntStr(se->opt, "deletejob") )
      deletejob(se);

   if( se->opt != NULL )
      optFree(&se->opt);

   if( se->curlheaders != NULL )
      curl_slist_free_all(se->curlheaders);

   if( se->curl != NULL )
      curl_easy_cleanup(se->curl);

   exitbuffer(&se->curlwritebuf);

   free(se->jobid);
   free(se->apikey);
   free(status);

   return rc;
}


void se_Initialize(void)
{
   gmoInitMutexes();
   gevInitMutexes();
   optInitMutexes();

   curl_global_init(CURL_GLOBAL_ALL);
}

void se_Finalize(void)
{
   curl_global_cleanup();

   gmoFiniMutexes();
   gevFiniMutexes();
   optFiniMutexes();
}
