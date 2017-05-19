#include <stdio.h>
#include <stdlib.h>

#include "gmomcc.h"
#include "gevmcc.h"

#include "convert.h"

int main(int argc, char** argv)
{
   gmoHandle_t gmo = NULL;
   gevHandle_t gev = NULL;
   int rc = EXIT_FAILURE;
   char buffer[1024];
   char lpfilename[300];
   int status;
   double objMinMaxFac;

   if (argc < 2)
   {
      printf("usage: %s <cntrlfile>\n", argv[0]);
      return 1;
   }

   /*
   * GAMS initialize GMO and GEV libraries
   */
   if (!gmoCreate(&gmo, buffer, sizeof(buffer)) || !gevCreate(&gev, buffer, sizeof(buffer)))
   {
      fprintf(stderr, "%s\n", buffer);
      goto TERMINATE;
   }

   /*
   * GAMS load control file
   */
   if (gevInitEnvironmentLegacy(gev, argv[1]))
   {
      fprintf(stderr, "Could not load control file %s\n", argv[1]);
      goto TERMINATE;
   }

   /*
   * GAMS let gmo know about gev
   */
   if (gmoRegisterEnvironment(gmo, gev, buffer))
   {
      fprintf(stderr, "Error registering GAMS Environment: %s\n", buffer);
      goto TERMINATE;
   }

   /*
   * GAMS load instance data
   */
   if (gmoLoadDataLegacy(gmo, buffer))
   {
      fprintf(stderr, "Could not load model data.\n");
      goto TERMINATE;
   }

   gevLogStat(gev, "This is GAMS/SolveEngine.");

   sprintf(lpfilename, "%sproblem.lp", gevGetStrOpt(gev, gevNameScrDir, buffer));
   printf("Writing LP to %s\n", lpfilename);
   writeLP(gmo, gev, lpfilename);


   /*
   * GAMS general
   */
   gmoObjStyleSet(gmo, gmoObjType_Fun);
   gmoObjReformSet(gmo, 1);
   gmoIndexBaseSet(gmo, 0);
   gmoSetNRowPerm(gmo); /* hide =N= rows */
   objMinMaxFac = (gmoSense(gmo) == gmoObj_Max) ? -1.0 : 1.0;

   gmoUnloadSolutionLegacy(gmo);

   rc = EXIT_SUCCESS;

TERMINATE:
   if(gmo != NULL)
      gmoFree(&gmo);
   if(gev != NULL)
      gevFree(&gev);

   return rc;
}
