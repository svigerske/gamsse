#ifndef GAMSSE_H_
#define GAMSSE_H_

typedef struct gamsse_s gamsse_t;
typedef gamsse_t se_Rec_t;  /* for GAMS buildsystem */

#ifdef __cplusplus
extern "C" {
#endif

void se_Free(
   gamsse_t** se
);

void se_Create(
   gamsse_t** se,
   char*      msgBuf,
   int        msgBufLen
);

int  se_CallSolver(
   gamsse_t*  se,
   void*      gmo
);

void se_Initialize(void);
void se_Finalize(void);

#ifdef __cplusplus
}
#endif

#endif /* GAMSSE_H_ */
