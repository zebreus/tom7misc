#ifndef _FCEULIB_ARKANOID_H
#define _FCEULIB_ARKANOID_H

struct InputC;
struct InputCFC;
struct FC;

extern InputC *CreateArkanoid(FC *fc, int w);
extern InputCFC *CreateArkanoidFC(FC *fc);

#endif
