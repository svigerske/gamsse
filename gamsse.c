/* TODO:
 * - interrupt job when timelimit reached
 * - delete job
 * - reuse curl handle
 * - read apikey from option file
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

/** ensures that there is space for at least size many additional characters in the buffer */
static
void ensurebuffer(
   buffer_t* buffer,
   size_t    size
   )
{
   assert(buffer != NULL);

   if( buffer->size <= buffer->length + size )
   {
      buffer->content = realloc(buffer->content, 2 * (buffer->length + size));
      if( buffer->content == NULL )
      {
         buffer->size = 0;
         /* TODO error */
      }
      buffer->size = 2 * (buffer->length + size); /* TODO nicer formula */
   }
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

   ensurebuffer(buffer, msglen+1);
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

   ensurebuffer(buffer, nmemb * size);
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

   ensurebuffer(&encodeprob->buffer, 2*msglen);  /* need 4/3*msglen many more bytes in buffer */
   cnt = base64_encode_block(msg, msglen, encodeprob->buffer.content + encodeprob->buffer.length, &encodeprob->es);
   encodeprob->buffer.length += cnt;

   return msglen;
}

#if 1
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

#if 0
   asctime_r(&tm, buf);
   buf[strlen(buf)-1] = '\0';
#endif

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
#endif

#if 0
static
char* getproblembase64(
   char* lpfilename
)
{
   FILE* f;
   long filesize;
   char* prob;
   base64_encodestate es;
   char buf[1024];
   int cnt;
   int pos;

   f = fopen(lpfilename, "r");
   if( f == NULL )
      return NULL;

   fseek(f, 0L, SEEK_END);
   filesize = ftell(f);
   fseek(f, 0L, SEEK_SET);

   prob = (char*) malloc(2*filesize);  /* we need 4/3 of the filesize for the encoded problem string */
   if( prob == NULL )
   {
      fclose(f);
      return NULL;
   }

   base64_init_encodestate(&es);
   pos = 0;
   while( 1 )
   {
      cnt = fread(buf, sizeof(char), sizeof(buf), f);
      if( cnt == 0 )
         break;
      assert(pos + 2*cnt <= 2*filesize);
      cnt = base64_encode_block(buf, cnt, prob+pos, &es);
      pos += cnt;
   }

   assert(pos + 3 < 2*filesize);
   cnt = base64_encode_blockend(prob+pos, &es);
   pos += cnt;

   fclose(f);

   return prob;
}
#endif

#if 0
static
char* convertbase64(
   char*  src,
   size_t srclen
)
{
   base64_encodestate es;
   char* dest;
   int cnt;

   dest = (char*) malloc(2*srclen);  /* we need 4/3 of the src length for the encoded problem string */
   if( dest == NULL )
      return NULL;

   base64_init_encodestate(&es);
   cnt = base64_encode_block(src, srclen, dest, &es);
   cnt += base64_encode_blockend(dest+cnt, &es);

   return dest;
}
#endif

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
   struct curl_slist* headers = NULL;
   int j;

   gevLog(gev, "Printing Joblist");

   curl = curl_easy_init();
   if( curl == NULL )
	   return; /* TODO report error */

   curl_easy_setopt(curl, CURLOPT_URL, "https://solve.satalia.com/api/v2/jobs");

   /* curl_easy_setopt(curl, CURLOPT_XOAUTH2_BEARER, apikey); */
   sprintf(strbuffer, "Authorization: api-key %s", apikey);
   headers = curl_slist_append(NULL, strbuffer);
   curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

   /* curl_easy_setopt(curl, CURLOPT_VERBOSE, 1); */
   curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, appendbufferCurl);
   curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buffer);

   curl_easy_perform(curl);

   if( buffer.content == NULL )  /* got no output at all */
      goto TERMINATE;

   ((char*)buffer.content)[buffer.length] = '\0';

   root = cJSON_Parse((char*)buffer.content);
   if( root == NULL )
      goto TERMINATE;

   jobs = cJSON_GetObjectItem(root, "jobs");
   if( jobs == NULL || !cJSON_IsArray(jobs) )
      goto TERMINATE;

   if( cJSON_GetArraySize(jobs) > 0 )
   {
      sprintf(strbuffer, "%40s %5s %10s %30s %30s %30s Used\n",
         "Job ID", "Algo", "Status", "Submittime", "Starttime", "Finishtime");
      gevLogPChar(gev, strbuffer);
   }
   else
   {
      gevLogPChar(gev, "No jobs in joblist.\n");
   }

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
#if 1
      char submittedbuf[32];
      char startedbuf[32];
      char finishedbuf[32];
#endif
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

      sprintf(strbuffer, "%s %5s %10s %30s %30s %30s %5d\n",
         id->valuestring,
         algo != NULL ? algo->valuestring : "N/A",
         status != NULL ? status->valuestring : "N/A",
#if 0
         submitted != NULL ? submitted->valuestring : "N/A",
         started != NULL ? started->valuestring : "N/A",
         finished != NULL ? finished->valuestring : "N/A",
#else
         formattime(submittedbuf, submitted != NULL ? submitted->valuestring : NULL),
         formattime(startedbuf, started != NULL ? started->valuestring : NULL),
         formattime(finishedbuf, finished != NULL ? finished->valuestring : NULL),
#endif
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

   gevLog(gev, "Creating Job");

   curl = curl_easy_init();
   if( curl == NULL )
      goto TERMINATE; /* TODO report error */

   curl_easy_setopt(curl, CURLOPT_URL, "https://solve.satalia.com/api/v2/jobs");

   /* curl_easy_setopt(curl, CURLOPT_XOAUTH2_BEARER, apikey); */
   sprintf(strbuffer, "Authorization: api-key %s", apikey);
   headers = curl_slist_append(NULL, strbuffer);
   curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

   /* curl_easy_setopt(curl, CURLOPT_VERBOSE, 1); */
   curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, appendbufferCurl);
   curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buffer);

   /* post fields */
   appendbuffer(&encodeprob.buffer, "{\"options\":{},\"problems\":[{\"name\":\"problem.lp\",\"data\": \"");

   /* append base64 encode of string in LP format (this will not be 0-terminated) */
   base64_init_encodestate(&encodeprob.es);
   writeLP(gmo, gev, appendbufferConvert, &encodeprob);
   ensurebuffer(&encodeprob.buffer, 2);
   encodeprob.buffer.length += base64_encode_blockend(encodeprob.buffer.content + encodeprob.buffer.length, &encodeprob.es);

   appendbuffer(&encodeprob.buffer, "\"}]}");
   curl_easy_setopt(curl, CURLOPT_POSTFIELDS, encodeprob.buffer.content);

   curl_easy_perform(curl);

   ((char*)buffer.content)[buffer.length] = '\0';

   root = cJSON_Parse((char*)buffer.content);
   if( root == NULL )
      goto TERMINATE;

   id = cJSON_GetObjectItem(root, "id");
   if( id == NULL || !cJSON_IsString(id) )
      goto TERMINATE;

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

#if 0
/* upload problem file */
static
void uploadfile(
   gevHandle_t gev,
   char* lpfilename,
   char* apikey,
   char* jobid
   )
{
   char strbuffer[1024];
   buffer_t buffer = BUFFERINIT;
   CURL* curl = NULL;
   struct curl_slist* headers = NULL;
   struct curl_httppost* post = NULL;
   struct curl_httppost* last = NULL;

   gevLog(gev, "Uploading problem file");

   curl = curl_easy_init();
   if( curl == NULL )
      return; /* TODO report error */

   sprintf(strbuffer, "https://solve.satalia.com/api/v1alpha/jobs/%s/files/problem.lp", jobid);
   curl_easy_setopt(curl, CURLOPT_URL, strbuffer);

   /* curl_easy_setopt(curl, CURLOPT_XOAUTH2_BEARER, apikey); */
   sprintf(strbuffer, "Authorization: Bearer %s", apikey);
   headers = curl_slist_append(NULL, strbuffer);
   curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

   /* curl_easy_setopt(curl, CURLOPT_VERBOSE, 1); */
   curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, appendbufferCurl);
   curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buffer);

   /* we need to set a multi-form message via PUT
    * with the curl tool, this was -X PUT -F file=@lpfilename
    */

   /* one (and the only) message part consists of the file to send */
   curl_formadd(&post, &last,
      CURLFORM_COPYNAME, "file",
      CURLFORM_FILE, lpfilename, CURLFORM_END);
   curl_easy_setopt(curl, CURLOPT_HTTPPOST, post);

   /* further, change from POST to PUT request */
   curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PUT");

   curl_easy_perform(curl);

   if( buffer.length > 0 )
   {
      /* something went wrong */
      ((char*)buffer.content)[buffer.length] = '\0';
      gevLog(gev, (char*)buffer.content);
   }

   if( curl != NULL )
      curl_easy_cleanup(curl);

   if( headers != NULL )
      curl_slist_free_all(headers);

   if( post != NULL )
      curl_formfree(post);

   exitbuffer(&buffer);
}
#endif

/* start job */
static
void startjob(
   gevHandle_t gev,
   char* apikey,
   char* jobid
   )
{
   char strbuffer[1024];
   buffer_t buffer = BUFFERINIT;
   CURL* curl = NULL;
   struct curl_slist* headers = NULL;

   gevLog(gev, "Starting job");

   curl = curl_easy_init();
   if( curl == NULL )
      return; /* TODO report error */

   sprintf(strbuffer, "https://solve.satalia.com/api/v2/jobs/%s/schedule", jobid);
   curl_easy_setopt(curl, CURLOPT_URL, strbuffer);

   /* curl_easy_setopt(curl, CURLOPT_XOAUTH2_BEARER, apikey); */
   sprintf(strbuffer, "Authorization: api-key %s", apikey);
   headers = curl_slist_append(NULL, strbuffer);
   curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

   /* curl_easy_setopt(curl, CURLOPT_VERBOSE, 1); */
   curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, appendbufferCurl);
   curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buffer);

   /* we want an empty POST request */
   curl_easy_setopt(curl, CURLOPT_POSTFIELDS, "");

   curl_easy_perform(curl);

   if( buffer.length > 0 )
   {
      /* something went wrong */ /* TODO if buffer is just "{}", then this is no problem */
      ((char*)buffer.content)[buffer.length] = '\0';
      gevLog(gev, (char*)buffer.content);
   }

   if( curl != NULL )
      curl_easy_cleanup(curl);

   if( headers != NULL )
      curl_slist_free_all(headers);

   exitbuffer(&buffer);
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

   gevLog(gev, "Query Job Status");

   curl = curl_easy_init();
   if( curl == NULL )
      return NULL; /* TODO report error */

   sprintf(strbuffer, "https://solve.satalia.com/api/v2/jobs/%s/status", jobid);
   curl_easy_setopt(curl, CURLOPT_URL, strbuffer);

   /* curl_easy_setopt(curl, CURLOPT_XOAUTH2_BEARER, apikey); */
   sprintf(strbuffer, "Authorization: api-key %s", apikey);
   headers = curl_slist_append(NULL, strbuffer);
   curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

   /* curl_easy_setopt(curl, CURLOPT_VERBOSE, 1); */
   curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, appendbufferCurl);
   curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buffer);

   curl_easy_perform(curl);

   ((char*)buffer.content)[buffer.length] = '\0';

   root = cJSON_Parse((char*)buffer.content);
   if( root == NULL )
      goto TERMINATE;

   status = cJSON_GetObjectItem(root, "status");
   if( status == NULL || !cJSON_IsString(status) )
      goto TERMINATE;

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
      return; /* TODO report error */

   sprintf(strbuffer, "https://solve.satalia.com/api/v2/jobs/%s/results", jobid);
   curl_easy_setopt(curl, CURLOPT_URL, strbuffer);

   /* curl_easy_setopt(curl, CURLOPT_XOAUTH2_BEARER, apikey); */
   sprintf(strbuffer, "Authorization: api-key %s", apikey);
   headers = curl_slist_append(NULL, strbuffer);
   curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

   /* curl_easy_setopt(curl, CURLOPT_VERBOSE, 1); */
   curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, appendbufferCurl);
   curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buffer);

   curl_easy_perform(curl);

   ((char*)buffer.content)[buffer.length] = '\0';

   gmoModelStatSet(gmo, gmoModelStat_NoSolutionReturned);
   gmoSolveStatSet(gmo, gmoSolveStat_SystemErr);

   root = cJSON_Parse((char*)buffer.content);
   if( root == NULL )
      goto TERMINATE;

   results = cJSON_GetObjectItem(root, "result");
   if( results == NULL )
      goto TERMINATE;

   status = cJSON_GetObjectItem(results, "status");
   if( status == NULL || !cJSON_IsString(status) )
      goto TERMINATE;

   gevLogPChar(gev, "Status: ");
   gevLog(gev, status->valuestring);

   if( strcmp(status->valuestring, "optimal") == 0 )
   {
      gmoModelStatSet(gmo, gmoModelStat_OptimalGlobal);
      gmoSolveStatSet(gmo, gmoSolveStat_Normal);

      variables = cJSON_GetObjectItem(results, "variables");
      if( variables != NULL )
      {
         cJSON* varvalpair;
         cJSON* varname;
         cJSON* val;
         int i;
         long int varidx;
         char* endptr;
         char namebuf[GMS_SSSIZE];

         for( i = 0; i < cJSON_GetArraySize(variables); ++i)
         {
            varvalpair = cJSON_GetArrayItem(variables, i);
            assert(varvalpair != NULL);

            varname = cJSON_GetObjectItem(varvalpair, "name");
            val = cJSON_GetObjectItem(varvalpair, "value");

            assert(cJSON_IsString(varname));
            assert(strlen(varname->valuestring) > 1);
            assert(cJSON_IsNumber(val));

            if( strcmp(varname->valuestring, "objvar") == 0 )
               continue;

            varidx = strtol(varname->valuestring+1, &endptr, 10);
            if( *endptr == '\0' )
               varidx = gmoGetjSolver(gmo, varidx-1);
            if( *endptr != '\0' || varidx < 0 || varidx >= gmoN(gmo) )
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

         /* TODO check whether we have a value for every variable */

         gmoCompleteSolution(gmo);
      }
   }
   else
   {
      /* TODO what other status could SolveEngine return? */
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

   /* check whether we have an API key for SolveEngine */
   apikey = getenv("SOLVEENGINE_APIKEY");
   if( apikey == NULL )
   {
      gevLogStat(gev, "No SolveEngine API key found in environment (SOLVEENGINE_APIKEY). Exiting.");
      goto TERMINATE;
   }
   else
   {
      gevLogPChar(gev, "API key: ");
      gevLog(gev, apikey);
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
      printf("Could not retrieve jobID\n");
      goto TERMINATE;
   }
   gevLogPChar(gev, "JobID: "); gevLog(gev, jobid);

   startjob(gev, apikey, jobid);

   do
   {
      sleep(1);
      free(status);
      status = jobstatus(gev, apikey, jobid);
      gevLogPChar(gev, "Job Status: "); gevLog(gev, status);
   }
   while(
     strcmp(status, "queued") == 0 ||   /* if queued, then we wait for available resources - hope that this wouldn't take too long */
     strcmp(status, "translating") == 0 ||
     strcmp(status, "started") == 0 ||
     strcmp(status, "starting") == 0 );

   if( strcmp(status, "completed") == 0 )
      getsolution(gmo, gev, apikey, jobid);

   /* TODO handle status = failed and status = stopped */

   /* TODO job logs to get solve_time */

   /* stopjob(gev, apikey, jobid); */

   gmoUnloadSolutionLegacy(gmo);

   rc = EXIT_SUCCESS;

TERMINATE:
   if(gmo != NULL)
      gmoFree(&gmo);
   if(gev != NULL)
      gevFree(&gev);

   curl_global_cleanup();

   gmoLibraryUnload();
   gevLibraryUnload();

   free(jobid);
   free(status);

   return rc;
}
