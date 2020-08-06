#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <math.h>
#include <float.h>  /* for DBL_MAX */

#include "convert.h"

#include "gmomcc.h"
#include "gevmcc.h"

#define MAX_PRINTLEN 561
#define PRINTLEN     100

const char* VARNAMEPREFIX[7] = { "x", "b", "i", "x", "x", "y", "j" };

RETURN convertEndLine(
   DECL_convertWriteFunc((*writefunc)),
   void*       writedata,
   char*       linebuffer,
   int*        linecnt
)
{
   /* should actually check whether writefunc return is strlen(linebuffer), but the only implementation only returns 0 or strlen(linebuffer) anyway */
   if( *linebuffer && writefunc(linebuffer, writedata) == 0 )
      return RETURN_ERROR_WRITEFUNC;

   if( writefunc("\n", writedata) != 1 )
      return RETURN_ERROR_WRITEFUNC;

   *linebuffer = '\0';
   *linecnt = 0;

   return RETURN_OK;
}

RETURN convertAppendLine(
   DECL_convertWriteFunc((*writefunc)),
   void*       writedata,
   char*       linebuffer,
   int*        linecnt,
   const char* extension
)
{
   int len;

   len = strlen(extension);
   assert(len < MAX_PRINTLEN);

   if( *linecnt + len >= MAX_PRINTLEN )
      CHECK( convertEndLine(writefunc, writedata, linebuffer, linecnt) );

   assert(strlen(linebuffer) + len < MAX_PRINTLEN);
   strcat(linebuffer, extension);

   *linecnt += len;

   if( *linecnt > PRINTLEN )
   {
      CHECK( convertEndLine(writefunc, writedata, linebuffer, linecnt) );
      CHECK( convertAppendLine(writefunc, writedata, linebuffer, linecnt, "  ") );
   }

   return RETURN_OK;
}

void convertGetVarName(
   gmoHandle_t gmo,
   int         idx,
   char*       buffer
   )
{
   sprintf(buffer, "%s%d", VARNAMEPREFIX[gmoGetVarTypeOne(gmo, idx)], idx);
}

void convertGetEquName(
   gmoHandle_t gmo,
   int         idx,
   char*       buffer
   )
{
   /* if original model was a scalar model written by convert, we would like to keep original equation names,
    * even though we removed the objective equation
    */
   sprintf(buffer, "e%d", idx);
}

#if 0
static
RETURN writeStatistics(
   gmoHandle_t gmo,
   DECL_convertWriteFunc((*writefunc)),
   void*       writedata,
   char*       linebuffer,
   int*        linecnt,
   const char* comment  /**< comment marker to put at begin of line */
)
{
   char buffer[PRINTLEN];

   convertAppendLine(writefunc, writedata, linebuffer, linecnt, comment);
   convertAppendLine(writefunc, writedata, linebuffer, linecnt, "Equation counts");
   convertEndLine(writefunc, writedata, linebuffer, linecnt);

   convertAppendLine(writefunc, writedata, linebuffer, linecnt, comment);
   convertAppendLine(writefunc, writedata, linebuffer, linecnt, "    Total        E        G        L        N        X        C        B");
   convertEndLine(writefunc, writedata, linebuffer, linecnt);

   sprintf(buffer, "%s%9d%9d%9d%9d%9d%9d%9d%9d",
      comment,
      gmoM(gmo),
      gmoGetEquTypeCnt(gmo, gmoequ_E),
      gmoGetEquTypeCnt(gmo, gmoequ_G),
      gmoGetEquTypeCnt(gmo, gmoequ_L),
      gmoGetEquTypeCnt(gmo, gmoequ_N),
      gmoGetEquTypeCnt(gmo, gmoequ_X),
      gmoGetEquTypeCnt(gmo, gmoequ_C),
      gmoGetEquTypeCnt(gmo, gmoequ_B));
   convertAppendLine(writefunc, writedata, linebuffer, linecnt, buffer);
   convertEndLine(writefunc, writedata, linebuffer, linecnt);

   convertAppendLine(writefunc, writedata, linebuffer, linecnt, comment);
   convertEndLine(writefunc, writedata, linebuffer, linecnt);

   convertAppendLine(writefunc, writedata, linebuffer, linecnt, comment);
   convertAppendLine(writefunc, writedata, linebuffer, linecnt, "Variable counts");
   convertEndLine(writefunc, writedata, linebuffer, linecnt);

   convertAppendLine(writefunc, writedata, linebuffer, linecnt, comment);
   convertAppendLine(writefunc, writedata, linebuffer, linecnt, "                 x        b        i      s1s      s2s       sc       si");
   convertEndLine(writefunc, writedata, linebuffer, linecnt);

   convertAppendLine(writefunc, writedata, linebuffer, linecnt, comment);
   convertAppendLine(writefunc, writedata, linebuffer, linecnt, "    Total     cont   binary  integer     sos1     sos2    scont     sint");
   convertEndLine(writefunc, writedata, linebuffer, linecnt);

   sprintf(buffer, "%s%9d%9d%9d%9d%9d%9d%9d%9d",
      comment,
      gmoN(gmo),
      gmoGetVarTypeCnt(gmo, gmovar_X),
      gmoGetVarTypeCnt(gmo, gmovar_B),
      gmoGetVarTypeCnt(gmo, gmovar_I),
      gmoGetVarTypeCnt(gmo, gmovar_S1),
      gmoGetVarTypeCnt(gmo, gmovar_S2),
      gmoGetVarTypeCnt(gmo, gmovar_SC),
      gmoGetVarTypeCnt(gmo, gmovar_SI));
   convertAppendLine(writefunc, writedata, linebuffer, linecnt, buffer);
   convertEndLine(writefunc, writedata, linebuffer, linecnt);

   convertAppendLine(writefunc, writedata, linebuffer, linecnt, comment);
   convertEndLine(writefunc, writedata, linebuffer, linecnt);

   convertAppendLine(writefunc, writedata, linebuffer, linecnt, comment);
   convertAppendLine(writefunc, writedata, linebuffer, linecnt, "Nonzero counts");
   convertEndLine(writefunc, writedata, linebuffer, linecnt);

   convertAppendLine(writefunc, writedata, linebuffer, linecnt, comment);
   convertAppendLine(writefunc, writedata, linebuffer, linecnt, "    Total    const       NL      DLL");
   convertEndLine(writefunc, writedata, linebuffer, linecnt);

   sprintf(buffer, "%s%9d%9d%9d%9d",
      comment,
      gmoNZ(gmo) + (gmoObjStyle(gmo) == gmoObjType_Var ? 1 : gmoObjNZ(gmo)),
      gmoNZ(gmo) - gmoNLNZ(gmo) + (gmoObjStyle(gmo) == gmoObjType_Var ? 1 : gmoObjNZ(gmo) - gmoObjNLNZ(gmo)),
      gmoNLNZ(gmo) + (gmoObjStyle(gmo) == gmoObjType_Var ? 0 : gmoObjNLNZ(gmo)),
      0);
   convertAppendLine(writefunc, writedata, linebuffer, linecnt, buffer);
   convertEndLine(writefunc, writedata, linebuffer, linecnt);

   convertAppendLine(writefunc, writedata, linebuffer, linecnt, comment);
   convertEndLine(writefunc, writedata, linebuffer, linecnt);

   return RETURN_OK;
}
#endif

static
RETURN writeBounds(
   gmoHandle_t gmo,
   DECL_convertWriteFunc((*writefunc)),
   void*       writedata,
   char*       linebuffer,
   int*        linecnt,
   int         doobjconstant
   )
{
   char buffer[PRINTLEN];
   int i;
   int printedsecname = 0;

   for( i = 0; i < gmoN(gmo); ++i )
   {
      double lb, ub;
      double defub;

      lb = gmoGetVarLowerOne(gmo, i);
      ub = gmoGetVarUpperOne(gmo, i);
      defub = (gmoGetVarTypeOne(gmo, i) == gmovar_B) ? 1.0 : gmoPinf(gmo);

      if( lb == 0.0 && ub == defub )  /* default .lp bounds -> skip */
         continue;

      if( !printedsecname )
      {
         CHECK( convertAppendLine(writefunc, writedata, linebuffer, linecnt, "Bounds") );
         CHECK( convertEndLine(writefunc, writedata, linebuffer, linecnt) );
         printedsecname = 1;
      }

      CHECK( convertAppendLine(writefunc, writedata, linebuffer, linecnt, " ") );

      if( lb == gmoMinf(gmo) && ub == gmoPinf(gmo) )
      {
         convertGetVarName(gmo, i, buffer);
         CHECK( convertAppendLine(writefunc, writedata, linebuffer, linecnt, buffer) );
         CHECK( convertAppendLine(writefunc, writedata, linebuffer, linecnt, " Free") );
         CHECK( convertEndLine(writefunc, writedata, linebuffer, linecnt) );

         continue;
      }

      if( lb != 0.0 && lb != ub )
      {
         if( lb == gmoMinf(gmo) )
            strcpy(buffer, "-inf");
         else
            sprintf(buffer, CONVERT_DOUBLEFORMAT, lb);
         CHECK( convertAppendLine(writefunc, writedata, linebuffer, linecnt, buffer) );
         CHECK( convertAppendLine(writefunc, writedata, linebuffer, linecnt, " <= ") );
      }

      convertGetVarName(gmo, i, buffer);
      CHECK( convertAppendLine(writefunc, writedata, linebuffer, linecnt, buffer) );

      if( ub != defub || lb == ub )
      {
         if( lb != ub )
            CHECK( convertAppendLine(writefunc, writedata, linebuffer, linecnt, " <= ") );
         else
            CHECK( convertAppendLine(writefunc, writedata, linebuffer, linecnt, " = ") );
         if( ub == gmoPinf(gmo) )
            sprintf(buffer, "+inf");  /* this could only happen for a binary variable with upper bound +inf... very unlikely */
         else
            sprintf(buffer, CONVERT_DOUBLEFORMAT, ub);
         CHECK( convertAppendLine(writefunc, writedata, linebuffer, linecnt, buffer) );
      }

      CHECK( convertEndLine(writefunc, writedata, linebuffer, linecnt) );
   }

   if( doobjconstant && gmoObjConst(gmo) != 0.0 )
   {
      if( !printedsecname )
      {
         CHECK( convertAppendLine(writefunc, writedata, linebuffer, linecnt, "Bounds") );
         CHECK( convertEndLine(writefunc, writedata, linebuffer, linecnt) );
         printedsecname = 1;
      }
      sprintf(buffer, " objconstant = " CONVERT_DOUBLEFORMAT, gmoObjConst(gmo));
      CHECK( convertAppendLine(writefunc, writedata, linebuffer, linecnt, buffer) );
      CHECK( convertEndLine(writefunc, writedata, linebuffer, linecnt) );
   }

   if( printedsecname )
      CHECK( convertEndLine(writefunc, writedata, linebuffer, linecnt) );

   return RETURN_OK;
}

static
RETURN writeVartypes(
   gmoHandle_t gmo,
   DECL_convertWriteFunc((*writefunc)),
   void*       writedata,
   char*       linebuffer,
   int*        linecnt
   )
{
   char buffer[PRINTLEN];
   int i;
   int printedsecname;

   if( gmoGetVarTypeCnt(gmo, gmovar_B) )
   {
      printedsecname = 0;
      for( i = 0; i < gmoN(gmo); ++i )
      {
         if( gmoGetVarTypeOne(gmo, i) != gmovar_B )
            continue;

         if( !printedsecname )
         {
            CHECK( convertAppendLine(writefunc, writedata, linebuffer, linecnt, "Binary") );
            CHECK( convertEndLine(writefunc, writedata, linebuffer, linecnt) );
            printedsecname = 1;
         }

         buffer[0] = ' ';
         convertGetVarName(gmo, i, buffer+1);
         CHECK( convertAppendLine(writefunc, writedata, linebuffer, linecnt, buffer) );

         if( *linecnt > PRINTLEN - 10 )
            CHECK( convertEndLine(writefunc, writedata, linebuffer, linecnt) );
      }
      if( printedsecname )
      {
         CHECK( convertEndLine(writefunc, writedata, linebuffer, linecnt) );
         CHECK( convertEndLine(writefunc, writedata, linebuffer, linecnt) );
      }
   }

   if( gmoGetVarTypeCnt(gmo, gmovar_I) )
   {
      printedsecname = 0;
      for( i = 0; i < gmoN(gmo); ++i )
      {
         if( gmoGetVarTypeOne(gmo, i) != gmovar_I )
            continue;

         if( !printedsecname )
         {
            CHECK( convertAppendLine(writefunc, writedata, linebuffer, linecnt, "General") );
            CHECK( convertEndLine(writefunc, writedata, linebuffer, linecnt) );
            printedsecname = 1;
         }

         buffer[0] = ' ';
         convertGetVarName(gmo, i, buffer+1);
         CHECK( convertAppendLine(writefunc, writedata, linebuffer, linecnt, buffer) );

         if( *linecnt > PRINTLEN - 10 )
            CHECK( convertEndLine(writefunc, writedata, linebuffer, linecnt) );
      }
      if( printedsecname )
      {
         CHECK( convertEndLine(writefunc, writedata, linebuffer, linecnt) );
         CHECK( convertEndLine(writefunc, writedata, linebuffer, linecnt) );
      }
   }

   if( gmoGetVarTypeCnt(gmo, gmovar_SC) )
   {
      printedsecname = 0;
      for( i = 0; i < gmoN(gmo); ++i )
      {
         if( gmoGetVarTypeOne(gmo, i) != gmovar_SC )
            continue;

         if( !printedsecname )
         {
            CHECK( convertAppendLine(writefunc, writedata, linebuffer, linecnt, "Semi") );
            CHECK( convertEndLine(writefunc, writedata, linebuffer, linecnt) );
            printedsecname = 1;
         }

         buffer[0] = ' ';
         convertGetVarName(gmo, i, buffer+1);
         CHECK( convertAppendLine(writefunc, writedata, linebuffer, linecnt, buffer) );

         if( *linecnt > PRINTLEN - 10 )
            CHECK( convertEndLine(writefunc, writedata, linebuffer, linecnt) );
      }
      if( printedsecname )
      {
         CHECK( convertEndLine(writefunc, writedata, linebuffer, linecnt) );
         CHECK( convertEndLine(writefunc, writedata, linebuffer, linecnt) );
      }
   }

   if( gmoGetVarTypeCnt(gmo, gmovar_S1) || gmoGetVarTypeCnt(gmo, gmovar_S2) )
   {
      int nsos1;
      int nsos2;
      int nsos;
      int nz;

      int* sostype;
      int* sosbeg;
      int* sosidx;
      double* soswt;
      int j;

      gmoGetSosCounts(gmo, &nsos1, &nsos2, &nz);
      nsos = nsos1 + nsos2;

      sostype = (int*) malloc(nsos * sizeof(int));
      sosbeg = (int*) malloc((nsos+1) * sizeof(int));
      sosidx = (int*) malloc(nz * sizeof(int));
      soswt = (double*) malloc(nz * sizeof(double));

      gmoGetSosConstraints(gmo, sostype, sosbeg, sosidx, soswt);

      CHECK( convertAppendLine(writefunc, writedata, linebuffer, linecnt, "SOS") );
      CHECK( convertEndLine(writefunc, writedata, linebuffer, linecnt) );

      for( i = 0; i < nsos; ++i )
      {
         sprintf(buffer, " set%03d: S%d::", i, sostype[i]);
         CHECK( convertAppendLine(writefunc, writedata, linebuffer, linecnt, buffer) );

         for( j = sosbeg[i]; j < sosbeg[i+1]; ++j )
         {
            buffer[0] = ' ';
            convertGetVarName(gmo, sosidx[j], buffer+1);
            sprintf(buffer + strlen(buffer), ":" CONVERT_DOUBLEFORMAT, soswt[j]);
            CHECK( convertAppendLine(writefunc, writedata, linebuffer, linecnt, buffer) );
         }

         CHECK( convertEndLine(writefunc, writedata, linebuffer, linecnt) );
      }

      CHECK( convertEndLine(writefunc, writedata, linebuffer, linecnt) );
   }

   return RETURN_OK;
}


static
RETURN writeLPFunction(
   gmoHandle_t gmo,
   DECL_convertWriteFunc((*writefunc)),
   void*       writedata,
   char*       linebuffer,
   int*        linecnt,
   int*        lincolidx,
   double*     lincoef,
   int         linnz,
   int*        quadcolidx,
   int*        quadrowidx,
   double*     quadcoef,
   int         quadnz
   )
{
   char buffer[PRINTLEN];
   int i;

   for( i = 0; i < linnz; ++i )
   {
      buffer[0] = '\0';

      if( lincoef[i] < 0.0 )
         strcat(buffer, "- ");
      else if( i > 0 )
         strcat(buffer, "+ ");

      if( fabs(lincoef[i]) != 1.0 )
         sprintf(buffer + strlen(buffer), CONVERT_DOUBLEFORMAT " ", fabs(lincoef[i]));

      convertGetVarName(gmo, lincolidx[i], buffer + strlen(buffer));

      if( i+1 < linnz )
         strcat(buffer, " ");

      CHECK( convertAppendLine(writefunc, writedata, linebuffer, linecnt, buffer) );
   }

   if( quadnz == 0 )
      return RETURN_OK;

   if( linnz > 0 )
      CHECK( convertAppendLine(writefunc, writedata, linebuffer, linecnt, " + ") );

   CHECK( convertAppendLine(writefunc, writedata, linebuffer, linecnt, "[ ") );

   for( i = 0; i < quadnz; ++i )
   {
      buffer[0] = '\0';

      /* because GMO is stupid */
      if( quadcolidx[i] == quadrowidx[i] )
         quadcoef[i] /= 2.0;

      if( quadcoef[i] < 0.0 )
         strcat(buffer, "- ");
      else if( i > 0 )
         strcat(buffer, "+ ");

      if( fabs(quadcoef[i]) != 1.0 )
         sprintf(buffer + strlen(buffer), CONVERT_DOUBLEFORMAT " ", fabs(quadcoef[i]));

      convertGetVarName(gmo, quadcolidx[i], buffer + strlen(buffer));
      if( quadcolidx[i] == quadrowidx[i] )
      {
         strcat(buffer, "^2");
      }
      else
      {
         strcat(buffer, " * ");
         convertGetVarName(gmo, quadrowidx[i], buffer + strlen(buffer));
      }
      strcat(buffer, " ");

      CHECK( convertAppendLine(writefunc, writedata, linebuffer, linecnt, buffer) );
   }

   CHECK( convertAppendLine(writefunc, writedata, linebuffer, linecnt, "]") );

   return RETURN_OK;
}

RETURN writeLP(
   gmoHandle_t gmo,
   gevHandle_t gev,
   DECL_convertWriteFunc((*writefunc)),
   void*          writedata
)
{
   char linebuffer[MAX_PRINTLEN];
   int  linecnt;
   char buffer[GMS_SSSIZE+10];

   int* lincolidx;
   double* lincoef;
   int linnz;

   int* quadrowidx;
   int* quadcolidx;
   double* quadcoef;
   int quadnz;

   int nlnz;
   int i;

   assert(gmo != NULL);
   assert(gev != NULL);
   assert(writefunc != NULL);

   gmoUseQSet(gmo, 1);

   if( (gmoObjStyle(gmo) == gmoObjType_Fun && gmoGetObjOrder(gmo) == gmoorder_NL) || gmoNLM(gmo) > 0 )
   {
      fputs("Instance has general nonlinear equations, cannot write in .lp format.\n", stderr);
      return RETURN_ERROR;
   }

   if( gmoGetVarTypeCnt(gmo, gmovar_SI) )
   {
      fputs("Instance has semi-integer variables, cannot write in .lp format.\n", stderr);
      return RETURN_ERROR;
   }

   if( gmoGetEquTypeCnt(gmo, gmoequ_C) )
   {
      /* TODO we could reformulate as quadratic */
      fputs("Instance has conic equations, cannot write in .lp format.\n", stderr);
      return RETURN_ERROR;
   }

   linebuffer[0] = '\0';
   linecnt = 0;

   /* CHECK( writeStatistics(gmo, writefunc, writedata, linebuffer, &linecnt, "\\ ") ); */

   lincolidx = (int*) malloc(gmoN(gmo) * sizeof(int));
   lincoef   = (double*) malloc(gmoN(gmo) * sizeof(double));
   quadrowidx = (int*) malloc(gmoMaxQNZ(gmo) * sizeof(int));
   quadcolidx = (int*) malloc(gmoMaxQNZ(gmo) * sizeof(int));
   quadcoef   = (double*) malloc(gmoMaxQNZ(gmo) * sizeof(double));

   if( gmoSense(gmo) == gmoObj_Min )
      CHECK( convertAppendLine(writefunc, writedata, linebuffer, &linecnt, "Minimize") );
   else
      CHECK( convertAppendLine(writefunc, writedata, linebuffer, &linecnt, "Maximize") );
   CHECK( convertEndLine(writefunc, writedata, linebuffer, &linecnt) );

   CHECK( convertAppendLine(writefunc, writedata, linebuffer, &linecnt, " obj: ") );

   for( i = 0; i < gmoN(gmo); ++i )
      lincolidx[i] = i;
   linnz = gmoN(gmo);

   quadnz = 0;
   if( gmoObjStyle(gmo) == gmoObjType_Var )
   {
      memset(lincoef, 0, gmoN(gmo) * sizeof(double));
      if( gmoObjVar(gmo) != gmoValNAInt(gmo) )
         lincoef[gmoObjVar(gmo)] = 1.0;
   }
   else
   {
      gmoGetObjVector(gmo, lincoef, NULL);

      /* gmoGetObjSparse(gmo, lincolidx, lincoef, NULL, &linnz, &nlnz); */

      if( gmoGetObjOrder(gmo) == gmoorder_Q )
      {
         quadnz = gmoObjQNZ(gmo);
         assert(quadnz <= gmoMaxQNZ(gmo));
         gmoGetObjQ(gmo, quadcolidx, quadrowidx, quadcoef);
         for( i = 0; i < quadnz; ++i )
            quadcoef[i] *= 2.0;
      }
   }

   CHECK( writeLPFunction(gmo, writefunc, writedata, linebuffer, &linecnt,
      lincolidx, lincoef, linnz,
      quadcolidx, quadrowidx, quadcoef, quadnz) );
   if( quadnz > 0 )
      CHECK( convertAppendLine(writefunc, writedata, linebuffer, &linecnt, "/2") );

   if( gmoObjConst(gmo) != 0.0 )
      CHECK( convertAppendLine(writefunc, writedata, linebuffer, &linecnt, " + objconstant") );

   CHECK( convertEndLine(writefunc, writedata, linebuffer, &linecnt) );

   CHECK( convertEndLine(writefunc, writedata, linebuffer, &linecnt) );

   CHECK( convertAppendLine(writefunc, writedata, linebuffer, &linecnt, "Subject To") );
   CHECK( convertEndLine(writefunc, writedata, linebuffer, &linecnt) );

   for( i = 0; i < gmoM(gmo); ++i )
   {
      buffer[0] = ' ';
      convertGetEquName(gmo, i, buffer+1);
      strcat(buffer, ": ");
      CHECK( convertAppendLine(writefunc, writedata, linebuffer, &linecnt, buffer) );

      gmoGetRowSparse(gmo, i, lincolidx, lincoef, NULL, &linnz, &nlnz);
      assert(nlnz == 0);

      quadnz = 0;
      if( gmoGetEquOrderOne(gmo, i) == gmoorder_Q )
      {
         quadnz = gmoGetRowQNZOne(gmo, i);
         assert(quadnz <= gmoMaxQNZ(gmo));
         gmoGetRowQ(gmo, i, quadcolidx, quadrowidx, quadcoef);
      }

      CHECK( writeLPFunction(gmo, writefunc, writedata, linebuffer, &linecnt,
         lincolidx, lincoef, linnz,
         quadcolidx, quadrowidx, quadcoef, quadnz) );

      switch( gmoGetEquTypeOne(gmo, i) )
      {
         case gmoequ_E :
            CHECK( convertAppendLine(writefunc, writedata, linebuffer, &linecnt, " = ") );
            break;
         case gmoequ_G :
            CHECK( convertAppendLine(writefunc, writedata, linebuffer, &linecnt, " >= ") );
            break;
         case gmoequ_L :
            CHECK( convertAppendLine(writefunc, writedata, linebuffer, &linecnt, " <= ") );
            break;
         default :
            /* should have been catched above */
            fputs("Unsupported equation type\n", stderr);
            return RETURN_ERROR;
      }

      sprintf(buffer, CONVERT_DOUBLEFORMAT, gmoGetRhsOne(gmo, i));
      CHECK( convertAppendLine(writefunc, writedata, linebuffer, &linecnt, buffer) );

      CHECK( convertEndLine(writefunc, writedata, linebuffer, &linecnt) );

   }
   CHECK( convertEndLine(writefunc, writedata, linebuffer, &linecnt) );

   free(lincolidx);
   free(lincoef);
   free(quadrowidx);
   free(quadcolidx);
   free(quadcoef);

   CHECK( writeBounds(gmo, writefunc, writedata, linebuffer, &linecnt, 1) );

   CHECK( writeVartypes(gmo, writefunc, writedata, linebuffer, &linecnt) );

   CHECK( convertAppendLine(writefunc, writedata, linebuffer, &linecnt, "End") );
   CHECK( convertEndLine(writefunc, writedata, linebuffer, &linecnt) );

   return RETURN_OK;
}
