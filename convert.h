#ifndef CONVERT_H_
#define CONVERT_H_

#define CONVERT_DOUBLEFORMAT "%.15g"

#define CHECK( x ) \
   do \
   { \
      RETURN _retcode = (x); \
      if( _retcode != RETURN_OK ) \
      { \
         fprintf(stderr, __FILE__ ":%d Error %d from call " #x "\n", __LINE__, _retcode); \
         return _retcode; \
      } \
   } while( 0 )

typedef enum {
    RETURN_OK = 0,
    RETURN_ERROR = 1
} RETURN;

struct gmoRec;
struct gevRec;

extern
void convertEndLine(
   FILE*       file,
   char*       linebuffer,
   int*        linecnt
);

extern
void convertAppendLine(
   FILE*       file,
   char*       linebuffer,
   int*        linecnt,
   const char* extension
);

extern
void convertGetVarName(
   struct gmoRec* gmo,
   int         idx,
   char*       buffer
   );

extern
void convertGetEquName(
   struct gmoRec* gmo,
   int         idx,
   char*       buffer
   );

extern
RETURN writeLP(
   struct gmoRec* gmo,
   struct gevRec* gev,
   const char*    filename
);

#endif /* CONVERT_H_ */
