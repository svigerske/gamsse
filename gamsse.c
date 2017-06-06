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

size_t appendcurlbuffer(
   void*  buf,
   size_t size,
   size_t nmemb,
   void*  userp)
{
   char* buffer = (char*)userp;
   assert(buffer != NULL);
   assert(size == 1);

   memcpy(buffer, buf, nmemb);
   buffer[nmemb] = '\0';

   return nmemb;
}

static
void printjoblist(
   gevHandle_t gev,
   char* apikey
)
{
   FILE* out;
   size_t len;
   char buffer[1024];
   CURL* curl = NULL;
   cJSON* root = NULL;
   cJSON* jobs = NULL;

   curl = curl_easy_init();
   if( !curl )
	   return; /* TODO report error */

   curl_easy_setopt(curl, CURLOPT_URL, "https://solve.satalia.com/api/v1alpha/jobs");
   /* curl_easy_setopt(curl, CURLOPT_XOAUTH2_BEARER, apikey); */
   sprintf(buffer, "Authorization: Bearer %s", apikey);
   curl_easy_setopt(curl, CURLOPT_HTTPHEADER, curl_slist_append(NULL, buffer));
   /* curl_easy_setopt(curl, CURLOPT_VERBOSE, 1); */

   curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, appendcurlbuffer);
   curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buffer);
   buffer[0] = '\0';

   curl_easy_perform(curl);

/*
   sprintf(buffer, "curl -H \"Authorization: Bearer %s\" https://solve.satalia.com/api/v1alpha/jobs", apikey);
   printf("Calling %s\n", buffer);
   out = popen(buffer, "r");
   len = fread(buffer, sizeof(char), sizeof(buffer), out);
   buffer[len] = '\0';
   pclose(out);
*/

   root = cJSON_Parse(buffer);
   if( root == NULL )
      goto TERMINATE;

   jobs = cJSON_GetObjectItem(root, "jobs");
   if( jobs == NULL )
      goto TERMINATE;

   gevLogPChar(gev, cJSON_Print(jobs));
   gevLogPChar(gev, "\n");

TERMINATE :
   if( root != NULL )
      cJSON_Delete(root);

   if( curl != NULL )
	   curl_easy_cleanup(curl);
}

/* returns job id */
static
char* submitjob(
   gevHandle_t gev,
   char* apikey
   )
{
   FILE* out;
   size_t len;
   char buffer[1024];
   char* loc;
   char* end;

   sprintf(buffer, "curl -d \"{\\\"options\\\":{},\\\"files\\\":[{\\\"name\\\": \\\"problem.lp\\\"}]}\" -H \"Authorization: Bearer %s\" https://solve.satalia.com/api/v1alpha/jobs", apikey);
   printf("Calling %s\n", buffer);
   out = popen(buffer, "r");
   len = fread(buffer, sizeof(char), sizeof(buffer), out);
   buffer[len] = '\0';
   gevLog(gev, buffer);
   pclose(out);

   loc = strstr(buffer, "job_id\"");
   if( loc == NULL )
      return NULL;
   loc += 7;  /* skip job_id" */

   loc = strstr(loc, "\"");  /* find quote that starts job id */
   if( loc == NULL )
      return NULL;
   ++loc; /* skip initial quote */

   end = strstr(loc, "\""); /* find closing quote */
   if( end == NULL )
      return NULL;
   *end = '\0';

   return strdup(loc);
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
   FILE* out;
   size_t len;
   char buffer[1024];
   char* loc;
   char* end;

   sprintf(buffer, "curl -X PUT -F file=@%s -H 'Authorization: Bearer %s' https://solve.satalia.com/api/v1alpha/jobs/%s/files/problem.lp", lpfilename, apikey, jobid);
   printf("Calling %s\n", buffer);
   out = popen(buffer, "r");
   len = fread(buffer, sizeof(char), sizeof(buffer), out);
   buffer[len] = '\0';
   gevLog(gev, buffer);  /* if everything went fine, then this is empty */
   pclose(out);
}

/* start job */
static
void startjob(
   gevHandle_t gev,
   char* apikey,
   char* jobid
   )
{
   FILE* out;
   size_t len;
   char buffer[1024];

   sprintf(buffer, "curl -X POST -H \"Authorization: Bearer %s\" https://solve.satalia.com/api/v1alpha/jobs/%s/start", apikey, jobid);
   printf("Calling %s\n", buffer);
   out = popen(buffer, "r");
   len = fread(buffer, sizeof(char), sizeof(buffer), out);
   buffer[len] = '\0';
   gevLog(gev, buffer);  /* if everything went fine, then this is empty */
   pclose(out);
}

/* job status */
static
char* jobstatus(
   gevHandle_t gev,
   char* apikey,
   char* jobid
   )
{
   FILE* out;
   size_t len;
   char buffer[1024];
   char* loc;
   char* end;

   sprintf(buffer, "curl -H \"Authorization: Bearer %s\" https://solve.satalia.com/api/v1alpha/jobs/%s/status", apikey, jobid);
   printf("Calling %s\n", buffer);
   out = popen(buffer, "r");
   len = fread(buffer, sizeof(char), sizeof(buffer), out);
   buffer[len] = '\0';

   loc = strstr(buffer, "status\"");
   if( loc == NULL )
      return NULL;
   loc += 7;  /* skip status" */

   loc = strstr(loc, "\"");  /* find quote that starts status */
   if( loc == NULL )
      return NULL;
   ++loc; /* skip initial quote */

   end = strstr(loc, "\""); /* find closing quote */
   if( end == NULL )
      return NULL;
   *end = '\0';

   return strdup(loc);
}

/* solution */
static
void getsolution(
   gevHandle_t gev,
   char* apikey,
   char* jobid
   )
{
   FILE* out;
   size_t len;
   char buffer[1024];
   char* loc;
   char* end;
   char* status;

   sprintf(buffer, "curl -H \"Authorization: Bearer %s\" https://solve.satalia.com/api/v1alpha/jobs/%s/solution", apikey, jobid);
   printf("Calling %s\n", buffer);
   out = popen(buffer, "r");
   len = fread(buffer, sizeof(char), sizeof(buffer), out);
   buffer[len] = '\0';
   pclose(out);

   /* check solution status */
   loc = strstr(buffer, "status\"");
   if( loc == NULL )
      return;
   loc += 7;  /* skip status" */

   loc = strstr(loc, "\"");  /* find quote that starts status */
   if( loc == NULL )
      return;
   ++loc; /* skip initial quote */

   end = strstr(loc, "\""); /* find closing quote */
   if( end == NULL )
      return;
   *end = '\0';

   /* print status */
   gevLogPChar(gev, "Status: "); gevLog(gev, loc);

   /* get variable values */
   loc = strstr(end+1, "variables\"");
   if( loc == NULL )
      return;
   loc += 10;
   loc = strstr(loc+10, "{\"name\"");
   while( loc != NULL )
   {
      end = strstr(loc, "}");
      if( end == NULL )
         break; /* this is odd */
      ++end;
      *end = '\0';

      /* print variable name and value pair */
      gevLog(gev, loc);

      loc = strstr(end+1, "{\"name\"");
   }
}

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
   char* loc;
   char* end;
   char* status;

   sprintf(buffer, "curl -X DELETE -H \"Authorization: Bearer %s\" https://solve.satalia.com/api/v1alpha/jobs/%s/stop", apikey, jobid);
   printf("Calling %s\n", buffer);
   out = popen(buffer, "r");
   len = fread(buffer, sizeof(char), sizeof(buffer), out);
   buffer[len] = '\0';
   gevLog(gev, buffer);  /* if everything went fine, then this is empty */
   pclose(out);
}

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
     strcmp(status, "queued") == 0 ||
     strcmp(status, "translating") == 0 ||
     strcmp(status, "started") == 0 ||
     strcmp(status, "starting") == 0 );

   if( strcmp(status, "completed") == 0 )
      getsolution(gev, apikey, jobid);

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

   return rc;
}
