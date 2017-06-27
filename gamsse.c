/* TODO:
 * - interrupt job when timelimit reached or Ctrl+C
 * - delete job
 * - reuse curl handle
 * - read apikey from option file
 * - option to enable curl verbose ouput
 * - check for curl error response
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

#include "gmomcc.h"
#include "gevmcc.h"

#include "convert.h"

typedef struct
{
   size_t  size;
   size_t  length;
   void*   content;
} buffer_t;

/** struct for writing problem into base64-encoded string */
typedef struct
{
   buffer_t           buffer;
   base64_encodestate es;
} encodeprob_t;

#define BUFFERINIT {0,0,NULL}

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
   if( ensurebuffer(&encodeprob->buffer, (int)1.5*msglen+2) < 1.5*msglen )
      return 0;

   cnt = base64_encode_block(msg, msglen, encodeprob->buffer.content + encodeprob->buffer.length, &encodeprob->es);
   encodeprob->buffer.length += cnt;

   return msglen;
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
void printjoblist(
   gevHandle_t gev,
   char* apikey
)
{
   char strbuffer[1024];
   buffer_t buffer = BUFFERINIT;
   CURL* curl = NULL;
   cJSON* root = NULL;
   cJSON* jobs = NULL;
   cJSON* total = NULL;
   struct curl_slist* headers = NULL;
   int j;

   curl = curl_easy_init();
   if( curl == NULL )
   {
      gevLogStat(gev, "printjoblist: Error in curl_easy_init()\n");
	   return;
   }

   curl_easy_setopt(curl, CURLOPT_URL, "https://solve.satalia.com/api/v2/jobs?per_page=2147483647");

   /* curl_easy_setopt(curl, CURLOPT_XOAUTH2_BEARER, apikey); */
   sprintf(strbuffer, "Authorization: api-key %s", apikey);
   headers = curl_slist_append(NULL, strbuffer);
   assert(headers != NULL);
   curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

   /* curl_easy_setopt(curl, CURLOPT_VERBOSE, 1); */
   curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, appendbufferCurl);
   curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buffer);

   curl_easy_perform(curl);

   if( buffer.content == NULL )  /* got no output at all */
   {
      gevLogStat(gev, "printjoblist: Failure retrieving joblist from SolveEngine.");
      goto TERMINATE;
   }

   ((char*)buffer.content)[buffer.length] = '\0';

   root = cJSON_Parse((char*)buffer.content);
   if( root == NULL )
   {
      gevLogStatPChar(gev, "printjoblist: Failure parsing joblist from SolveEngine. Content: ");
      gevLogStatPChar(gev, buffer.content);
      gevLogStatPChar(gev, "\n");
      goto TERMINATE;
   }

   total = cJSON_GetObjectItem(root, "total");
   if( total == NULL || !cJSON_IsNumber(total) )
      sprintf(strbuffer, "Printing Joblist:\n");
   else
      sprintf(strbuffer, "Printing Joblist: %d jobs in total\n", total->valueint);
   gevLogPChar(gev, strbuffer);

   if( total->valueint == 0 )
      goto TERMINATE;

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

   if( curl != NULL )
	   curl_easy_cleanup(curl);

   if( headers != NULL )
      curl_slist_free_all(headers);

   exitbuffer(&buffer);
}

/* returns job id */
static
char* submitjob(
   gmoHandle_t gmo,
   gevHandle_t gev,
   char* apikey
   )
{
   char strbuffer[1024];
   buffer_t buffer = BUFFERINIT;
   CURL* curl = NULL;
   cJSON* root = NULL;
   cJSON* id = NULL;
   struct curl_slist* headers = NULL;
   char* idstr = NULL;
   char* postfields = NULL;
   encodeprob_t encodeprob = { .buffer = BUFFERINIT };

   gevLog(gev, "Submitting Job");

   curl = curl_easy_init();
   if( curl == NULL )
   {
      gevLogStat(gev, "submitjob: Error in curl_easy_init()\n");
      return NULL;
   }

   curl_easy_setopt(curl, CURLOPT_URL, "https://solve.satalia.com/api/v2/jobs");

   /* curl_easy_setopt(curl, CURLOPT_XOAUTH2_BEARER, apikey); */
   sprintf(strbuffer, "Authorization: api-key %s", apikey);
   headers = curl_slist_append(NULL, strbuffer);
   assert(headers != NULL);
   curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

   /* curl_easy_setopt(curl, CURLOPT_VERBOSE, 1); */
   curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, appendbufferCurl);
   curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buffer);

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
   curl_easy_setopt(curl, CURLOPT_POSTFIELDS, encodeprob.buffer.content);

   /* TODO enable status bar if postfields length is rather large */

   curl_easy_perform(curl);

   if( buffer.content == NULL )  /* got no output at all */
   {
      gevLogStat(gev, "submitjob: Failure submitting job to SolveEngine.");
      goto TERMINATE;
   }
   ((char*)buffer.content)[buffer.length] = '\0';

   root = cJSON_Parse((char*)buffer.content);
   if( root == NULL )
   {
      gevLogStat(gev, "submitjob: Failure parsing response from SolveEngine.");
      goto TERMINATE;
   }

   id = cJSON_GetObjectItem(root, "id");
   if( id == NULL || !cJSON_IsString(id) )
   {
      gevLogStat(gev, "submitjob: Failure obtaining job id from SolveEngine.");
      goto TERMINATE;
   }

   idstr = strdup(id->valuestring);

TERMINATE :
   if( root != NULL )
      cJSON_Delete(root);

   if( curl != NULL )
      curl_easy_cleanup(curl);

   if( headers != NULL )
      curl_slist_free_all(headers);

   if( postfields != NULL )
      postfields = NULL;

   exitbuffer(&buffer);
   exitbuffer(&encodeprob.buffer);

   return idstr;
}

/* schedule a job to be run */
static
RETURN schedulejob(
   gevHandle_t gev,
   char* apikey,
   char* jobid
   )
{
   char strbuffer[1024];
   buffer_t buffer = BUFFERINIT;
   CURL* curl = NULL;
   struct curl_slist* headers = NULL;
   int rc = RETURN_ERROR;

   curl = curl_easy_init();
   if( curl == NULL )
   {
      gevLogStat(gev, "schedulejob: Error in curl_easy_init()\n");
      goto TERMINATE;
   }

   sprintf(strbuffer, "https://solve.satalia.com/api/v2/jobs/%s/schedule", jobid);
   curl_easy_setopt(curl, CURLOPT_URL, strbuffer);

   /* curl_easy_setopt(curl, CURLOPT_XOAUTH2_BEARER, apikey); */
   sprintf(strbuffer, "Authorization: api-key %s", apikey);
   headers = curl_slist_append(NULL, strbuffer);
   assert(headers != NULL);
   curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

   /* curl_easy_setopt(curl, CURLOPT_VERBOSE, 1); */
   curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, appendbufferCurl);
   curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buffer);

   /* we want an empty POST request */
   curl_easy_setopt(curl, CURLOPT_POSTFIELDS, "");

   curl_easy_perform(curl);

   if( buffer.length > 2 )
   {
      /* something went wrong */
      assert(buffer.content != NULL);
      ((char*)buffer.content)[buffer.length] = '\0';
      gevLogStatPChar(gev, "schedulejob: Failed to schedule job: ");
      gevLogStatPChar(gev, (char*)buffer.content);
      gevLogStatPChar(gev, "\n");
   }

   rc = RETURN_OK;

TERMINATE:
   if( curl != NULL )
      curl_easy_cleanup(curl);

   if( headers != NULL )
      curl_slist_free_all(headers);

   exitbuffer(&buffer);

   return rc;
}

/* job status */
static
char* jobstatus(
   gevHandle_t gev,
   char* apikey,
   char* jobid
   )
{
   char strbuffer[1024];
   buffer_t buffer = BUFFERINIT;
   CURL* curl = NULL;
   struct curl_slist* headers = NULL;
   cJSON* root = NULL;
   cJSON* status = NULL;
   char* statusstr = NULL;

   curl = curl_easy_init();
   if( curl == NULL )
   {
      gevLogStat(gev, "jobstatus: Error in curl_easy_init()\n");
      return NULL;
   }

   sprintf(strbuffer, "https://solve.satalia.com/api/v2/jobs/%s/status", jobid);
   curl_easy_setopt(curl, CURLOPT_URL, strbuffer);

   /* curl_easy_setopt(curl, CURLOPT_XOAUTH2_BEARER, apikey); */
   sprintf(strbuffer, "Authorization: api-key %s", apikey);
   headers = curl_slist_append(NULL, strbuffer);
   assert(headers != NULL);
   curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

   /* curl_easy_setopt(curl, CURLOPT_VERBOSE, 1); */
   curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, appendbufferCurl);
   curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buffer);

   curl_easy_perform(curl);

   if( buffer.content == NULL )
   {
      gevLogStat(gev, "jobstatus: Error retrieving job status.");
      goto TERMINATE;
   }
   ((char*)buffer.content)[buffer.length] = '\0';

   root = cJSON_Parse((char*)buffer.content);
   if( root == NULL )
   {
      gevLogStatPChar(gev, "jobstatus: Error parsing job status from answer ");
      gevLogStatPChar(gev, buffer.content);
      gevLogStatPChar(gev, "\n");
      goto TERMINATE;
   }

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

   if( curl != NULL )
      curl_easy_cleanup(curl);

   if( headers != NULL )
      curl_slist_free_all(headers);

   exitbuffer(&buffer);

   return statusstr;
}

/* solution */
static
void getsolution(
   gmoHandle_t gmo,
   gevHandle_t gev,
   char* apikey,
   char* jobid
   )
{
   char strbuffer[1024];
   buffer_t buffer = BUFFERINIT;
   CURL* curl = NULL;
   struct curl_slist* headers = NULL;
   cJSON* root = NULL;
   cJSON* results = NULL;
   cJSON* status = NULL;
   cJSON* variables = NULL;

   gevLog(gev, "Query Solution");

   curl = curl_easy_init();
   if( curl == NULL )
   {
      gevLogStat(gev, "getsolution: Error in curl_easy_init()\n");
      return;
   }

   sprintf(strbuffer, "https://solve.satalia.com/api/v2/jobs/%s/results", jobid);
   curl_easy_setopt(curl, CURLOPT_URL, strbuffer);

   /* curl_easy_setopt(curl, CURLOPT_XOAUTH2_BEARER, apikey); */
   sprintf(strbuffer, "Authorization: api-key %s", apikey);
   headers = curl_slist_append(NULL, strbuffer);
   assert(headers != NULL);
   curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

   /* curl_easy_setopt(curl, CURLOPT_VERBOSE, 1); */
   curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, appendbufferCurl);
   curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buffer);

   curl_easy_perform(curl);

   if( buffer.content == NULL )
   {
      gevLogStat(gev, "getsolution: Error retrieving solution.");
      goto TERMINATE;
   }

   ((char*)buffer.content)[buffer.length] = '\0';

   root = cJSON_Parse((char*)buffer.content);
   if( root == NULL )
   {
      gevLogStatPChar(gev, "getsolution: Error parsing solution from answer ");
      gevLogStatPChar(gev, buffer.content);
      gevLogStatPChar(gev, "\n");
      goto TERMINATE;
   }

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

   gevLogPChar(gev, "Status: ");
   gevLog(gev, status->valuestring);

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

   if( curl != NULL )
      curl_easy_cleanup(curl);

   if( headers != NULL )
      curl_slist_free_all(headers);

   exitbuffer(&buffer);
}

#if 0
/* stop a started job, doesn't seem to delete the job */
static
void stopjob(
   gevHandle_t gev,
   char* apikey,
   char* jobid
   )
{
   FILE* out;
   size_t len;
   char buffer[1024];

   sprintf(buffer, "curl -X DELETE -H \"Authorization: api-key %s\" https://solve.satalia.com/api/v2/jobs/%s/stop", apikey, jobid);
   printf("Calling %s\n", buffer);
   out = popen(buffer, "r");
   len = fread(buffer, sizeof(char), sizeof(buffer), out);
   buffer[len] = '\0';
   gevLog(gev, buffer);  /* if everything went fine, then this is empty */
   pclose(out);
}
#endif

int main(int argc, char** argv)
{
   gmoHandle_t gmo = NULL;
   gevHandle_t gev = NULL;
   int rc = EXIT_FAILURE;
   char buffer[1024];
   char* apikey;
   char* jobid = NULL;
   char* status = NULL;

   if (argc < 2)
   {
      printf("usage: %s <cntrlfile>\n", argv[0]);
      return 1;
   }

   curl_global_init(CURL_GLOBAL_ALL);

   /* GAMS initialize GMO and GEV libraries */
   if( !gmoCreate(&gmo, buffer, sizeof(buffer)) || !gevCreate(&gev, buffer, sizeof(buffer)) )
   {
      fprintf(stderr, "%s\n", buffer);
      goto TERMINATE;
   }

   /* GAMS load control file */
   if( gevInitEnvironmentLegacy(gev, argv[1]) )
   {
      fprintf(stderr, "Could not load control file %s\n", argv[1]);
      goto TERMINATE;
   }

   /* GAMS let gmo know about gev */
   if( gmoRegisterEnvironment(gmo, gev, buffer) )
   {
      fprintf(stderr, "Error registering GAMS Environment: %s\n", buffer);
      goto TERMINATE;
   }

   /* GAMS load instance data */
   if( gmoLoadDataLegacy(gmo, buffer) )
   {
      fprintf(stderr, "Could not load model data.\n");
      goto TERMINATE;
   }

   gevLogStat(gev, "This is the GAMS link to Satalia SolveEngine.");

   gmoModelStatSet(gmo, gmoModelStat_NoSolutionReturned);
   gmoSolveStatSet(gmo, gmoSolveStat_SystemErr);

   /* check whether we have an API key for SolveEngine */
   apikey = getenv("SOLVEENGINE_APIKEY");
   if( apikey == NULL )
   {
      gevLogStat(gev, "No SolveEngine API key found in environment (SOLVEENGINE_APIKEY). Exiting.");
      goto TERMINATE;
   }
   else if( strlen(apikey) > 50 ) /* mine is 45 chars */
   {
      gevLogStat(gev, "Invalid API key found in environment (SOLVEENGINE_APIKEY): too long. Exiting.");
      goto TERMINATE;
   }
   else
   {
      gevLog(gev, "Found API key in environment.");
   }

   printjoblist(gev, apikey);

   /* get the problem into a normal form */
   gmoObjStyleSet(gmo, gmoObjType_Fun);
   gmoObjReformSet(gmo, 1);
   gmoIndexBaseSet(gmo, 0);
   gmoSetNRowPerm(gmo); /* hide =N= rows */

   jobid = submitjob(gmo, gev, apikey);
   if( jobid == NULL )
   {
      gevLogStat(gev, "Could not retrieve jobID.");
      goto TERMINATE;
   }
   gevLogPChar(gev, "JobID: "); gevLog(gev, jobid);

   if( schedulejob(gev, apikey, jobid) != RETURN_OK )
      goto TERMINATE;

   do
   {
      sleep(1);
      free(status);
      status = jobstatus(gev, apikey, jobid);
      gevLogPChar(gev, "Job Status: ");
      gevLog(gev, status != NULL ? status : "UNKNOWN");
   }
   while( status != NULL && (
     strcmp(status, "queued") == 0 ||   /* if queued, then we wait for available resources - hope that this wouldn't take too long */
     strcmp(status, "translating") == 0 ||
     strcmp(status, "started") == 0 ||
     strcmp(status, "starting") == 0) );

   if( status != NULL && strcmp(status, "completed") == 0 )
      getsolution(gmo, gev, apikey, jobid);

   /* TODO handle status = failed and status = stopped */

   /* TODO job logs to get solve_time */

   /* stopjob(gev, apikey, jobid); */


   rc = EXIT_SUCCESS;

TERMINATE:
   if(gmo != NULL)
   {
      gmoUnloadSolutionLegacy(gmo);
      gmoFree(&gmo);
   }
   if(gev != NULL)
      gevFree(&gev);

   curl_global_cleanup();

   gmoLibraryUnload();
   gevLibraryUnload();

   free(jobid);
   free(status);

   return rc;
}
