/* TODO:
 * - switch to v2 API
 * - interrupt job when timelimit reached
 * - delete job
 * - reuse curl handle
 * - there should be no need to write problem to .lp file to disk
 * - read apikey from option file
 *
 * Links:
 * - https://curl.haxx.se/libcurl/c/libcurl.html
 * - https://github.com/DaveGamble/cJSON/tree/v1.5.3
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>  /* for sleep() */

#include "curl/curl.h"
#include "cJSON.h"

#include "gmomcc.h"
#include "gevmcc.h"

#include "convert.h"

typedef struct
{
   size_t  size;
   size_t  length;
   void*   content;
} buffer_t;

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

static
size_t appendbuffer(
   void*  curlbuf,
   size_t size,
   size_t nmemb,
   void*  buf)
{
   buffer_t* buffer = (buffer_t*)buf;

   assert(curlbuf != NULL || nmemb == 0);
   assert(buffer != NULL);

   if( buffer->size <= buffer->length + nmemb * size )
   {
      buffer->content = realloc(buffer->content, 2 * (buffer->length + nmemb * size));
      if( buffer->content == NULL )
      {
         buffer->size = 0;
         return 0; /* TODO error */
      }
      buffer->size = 2 * (buffer->length + nmemb * size); /* TODO nicer formula */
   }

   memcpy(buffer->content + buffer->length, curlbuf, nmemb * size);
   buffer->length += nmemb * size;

   return nmemb;
}

static
char* formattime(
   char*  buf,     /**< buffer to store time, must be at least 26 chars */
   time_t seconds
   )
{
   /* treat 0 as special value */
   if( seconds == 0 )
   {
      sprintf(buf, "N/A");
      return buf;
   }

   if( ctime_r(&seconds, buf) == NULL )
   {
      sprintf(buf, "N/A");
      return buf;
   }

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
   struct curl_slist* headers = NULL;
   int j;

   gevLog(gev, "Printing Joblist");

   curl = curl_easy_init();
   if( curl == NULL )
	   return; /* TODO report error */

   curl_easy_setopt(curl, CURLOPT_URL, "https://solve.satalia.com/api/v1alpha/jobs");

   /* curl_easy_setopt(curl, CURLOPT_XOAUTH2_BEARER, apikey); */
   sprintf(strbuffer, "Authorization: Bearer %s", apikey);
   headers = curl_slist_append(NULL, strbuffer);
   curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

   /* curl_easy_setopt(curl, CURLOPT_VERBOSE, 1); */
   curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, appendbuffer);
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
      sprintf(strbuffer, "%40s %5s %10s %30s %30s %30s\n",
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
      char submittedbuf[32];
      char startedbuf[32];
      char finishedbuf[32];

      job = cJSON_GetArrayItem(jobs, j);
      assert(job != NULL);

      id = cJSON_GetObjectItem(job, "id");
      if( id == NULL )
         continue;

      status = cJSON_GetObjectItem(job, "status");
      algo = cJSON_GetObjectItem(job, "algo");
      status = cJSON_GetObjectItem(job, "status");
      submitted = cJSON_GetObjectItem(job, "submited");
      started = cJSON_GetObjectItem(job, "started");
      finished = cJSON_GetObjectItem(job, "finished");

      sprintf(strbuffer, "%s %5s %10s %30s %30s %30s\n",
         id->valuestring,
         algo != NULL ? algo->valuestring : "N/A",
         status != NULL ? status->valuestring : "N/A",
         formattime(submittedbuf, submitted != NULL ? submitted->valueint : 0),
         formattime(startedbuf, started != NULL ? started->valueint : 0),
         formattime(finishedbuf, finished != NULL ? finished->valueint : 0)
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

   gevLog(gev, "Creating Job");

   curl = curl_easy_init();
   if( curl == NULL )
      return NULL; /* TODO report error */

   curl_easy_setopt(curl, CURLOPT_URL, "https://solve.satalia.com/api/v1alpha/jobs");

   /* curl_easy_setopt(curl, CURLOPT_XOAUTH2_BEARER, apikey); */
   sprintf(strbuffer, "Authorization: Bearer %s", apikey);
   headers = curl_slist_append(NULL, strbuffer);
   curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

   /* curl_easy_setopt(curl, CURLOPT_VERBOSE, 1); */
   curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, appendbuffer);
   curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buffer);

   curl_easy_setopt(curl, CURLOPT_POSTFIELDS, "{\"options\":{},\"files\":[{\"name\":\"problem.lp\"}]}");

   curl_easy_perform(curl);

   ((char*)buffer.content)[buffer.length] = '\0';

   root = cJSON_Parse((char*)buffer.content);
   if( root == NULL )
      goto TERMINATE;

   id = cJSON_GetObjectItem(root, "job_id");  /* NOTE: will change to "id" with API 2 */
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

   exitbuffer(&buffer);

   return idstr;
}

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
   curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, appendbuffer);
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

   sprintf(strbuffer, "https://solve.satalia.com/api/v1alpha/jobs/%s/start", jobid);
   curl_easy_setopt(curl, CURLOPT_URL, strbuffer);

   /* curl_easy_setopt(curl, CURLOPT_XOAUTH2_BEARER, apikey); */
   sprintf(strbuffer, "Authorization: Bearer %s", apikey);
   headers = curl_slist_append(NULL, strbuffer);
   curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

   /* curl_easy_setopt(curl, CURLOPT_VERBOSE, 1); */
   curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, appendbuffer);
   curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buffer);

   /* we want an empty POST request */
   curl_easy_setopt(curl, CURLOPT_POSTFIELDS, "");

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

   sprintf(strbuffer, "https://solve.satalia.com/api/v1alpha/jobs/%s/status", jobid);
   curl_easy_setopt(curl, CURLOPT_URL, strbuffer);

   /* curl_easy_setopt(curl, CURLOPT_XOAUTH2_BEARER, apikey); */
   sprintf(strbuffer, "Authorization: Bearer %s", apikey);
   headers = curl_slist_append(NULL, strbuffer);
   curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

   /* curl_easy_setopt(curl, CURLOPT_VERBOSE, 1); */
   curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, appendbuffer);
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

   sprintf(strbuffer, "https://solve.satalia.com/api/v1alpha/jobs/%s/solution", jobid);
   curl_easy_setopt(curl, CURLOPT_URL, strbuffer);

   /* curl_easy_setopt(curl, CURLOPT_XOAUTH2_BEARER, apikey); */
   sprintf(strbuffer, "Authorization: Bearer %s", apikey);
   headers = curl_slist_append(NULL, strbuffer);
   curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

   /* curl_easy_setopt(curl, CURLOPT_VERBOSE, 1); */
   curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, appendbuffer);
   curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buffer);

   curl_easy_perform(curl);

   ((char*)buffer.content)[buffer.length] = '\0';

   gmoModelStatSet(gmo, gmoModelStat_NoSolutionReturned);
   gmoSolveStatSet(gmo, gmoSolveStat_SystemErr);

   root = cJSON_Parse((char*)buffer.content);
   if( root == NULL )
      goto TERMINATE;

   results = cJSON_GetObjectItem(root, "results"); /* NOTE: will change to "result" in API 2 */
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

   sprintf(buffer, "curl -X DELETE -H \"Authorization: Bearer %s\" https://solve.satalia.com/api/v1alpha/jobs/%s/stop", apikey, jobid);
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
   char lpfilename[300];
   char* apikey;
   char* jobid;
   char* status;

   if (argc < 2)
   {
      printf("usage: %s <cntrlfile>\n", argv[0]);
      return 1;
   }

   curl_global_init(CURL_GLOBAL_ALL);

   /* GAMS initialize GMO and GEV libraries */
   if (!gmoCreate(&gmo, buffer, sizeof(buffer)) || !gevCreate(&gev, buffer, sizeof(buffer)))
   {
      fprintf(stderr, "%s\n", buffer);
      goto TERMINATE;
   }

   /* GAMS load control file */
   if (gevInitEnvironmentLegacy(gev, argv[1]))
   {
      fprintf(stderr, "Could not load control file %s\n", argv[1]);
      goto TERMINATE;
   }

   /* GAMS let gmo know about gev */
   if (gmoRegisterEnvironment(gmo, gev, buffer))
   {
      fprintf(stderr, "Error registering GAMS Environment: %s\n", buffer);
      goto TERMINATE;
   }

   /* GAMS load instance data */
   if (gmoLoadDataLegacy(gmo, buffer))
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

   /* make LP file from problem */
   sprintf(lpfilename, "%sproblem.lp", gevGetStrOpt(gev, gevNameScrDir, buffer));
   printf("Writing LP to %s\n", lpfilename);
   writeLP(gmo, gev, lpfilename);

   /* remove(lpfilename); */

   jobid = submitjob(gev, apikey);
   if( jobid == NULL )
   {
      printf("Could not retrieve jobID\n");
      goto TERMINATE;
   }
   gevLogPChar(gev, "JobID: "); gevLog(gev, jobid);

   uploadfile(gev, lpfilename, apikey, jobid);

   startjob(gev, apikey, jobid);

   do
   {
      sleep(1);
      status = jobstatus(gev, apikey, jobid);
      gevLogPChar(gev, "Job Status: "); gevLog(gev, status);
   }
   while(
     /* strcmp(status, "queued") == 0 || */   /* TODO queued means, that "startjob" failed */
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

   return rc;
}
