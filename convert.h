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

#define DECL_convertWriteFunc(x) size_t x ( \
   const char* msg, \
   void*       writedata \
)

extern
void convertEndLine(
   DECL_convertWriteFunc((*writefunc)),
   void*       writedata,
   char*       linebuffer,
   int*        linecnt
);

extern
void convertAppendLine(
   DECL_convertWriteFunc((*writefunc)),
   void*       writedata,
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
   DECL_convertWriteFunc((*writefunc)),
   void*          writedata
);

#endif /* CONVERT_H_ */
