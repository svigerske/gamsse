#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gmomcc.h"
#include "gevmcc.h"

#include "convert.h"

static
void printjoblist(
   gevHandle_t gev,
   char* apikey
)
{
   FILE* out;
   size_t len;
   char buffer[1024];

   sprintf(buffer, "curl -H \"Authorization: Bearer %s\" https://solve.satalia.com/api/v1alpha/jobs", apikey);
   out = popen(buffer, "r");
   len = fread(buffer, sizeof(char), sizeof(buffer), out);
   buffer[len] = '\0';
   gevLog(gev, buffer);
   pclose(out);
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

/*
   sprintf(buffer, "curl -d \"{\\\"options\\\":{},\\\"files\\\":[{\\\"name\\\": \\\"problem.lp\\\"}]}\" -H \"Authorization: Bearer %s\" https://solve.satalia.com/api/v1alpha/jobs", apikey);
   printf("Calling %s\n", buffer);
   out = popen(buffer, "r");
   len = fread(buffer, sizeof(char), sizeof(buffer), out);
   buffer[len] = '\0';
   gevLog(gev, buffer);
   pclose(out);
*/
   sprintf(buffer, "{\"job_id\":\"014259e50e8e7204b183ef6eb40aea86f8ad1eab\"}");

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

int main(int argc, char** argv)
{
   gmoHandle_t gmo = NULL;
   gevHandle_t gev = NULL;
   int rc = EXIT_FAILURE;
   char buffer[1024];
   char lpfilename[300];
   char* apikey;
   char* jobid;

   if (argc < 2)
   {
      printf("usage: %s <cntrlfile>\n", argv[0]);
      return 1;
   }

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

   /* job status */

   /* solution */

   gmoUnloadSolutionLegacy(gmo);

   rc = EXIT_SUCCESS;

TERMINATE:
   if(gmo != NULL)
      gmoFree(&gmo);
   if(gev != NULL)
      gevFree(&gev);

   return rc;
}
