
#ifndef _ECM_H
#define _ECM_H


enum eEcmResult
{
  FACTOR_NOT_FOUND = 0,
  FACTOR_FOUND,
  FACTOR_NOT_FOUND_GCD,
  CHANGE_TO_SIQS,
};

enum eEcmResult ecmCurve(int* pEC, int* pNextEC);

#endif
