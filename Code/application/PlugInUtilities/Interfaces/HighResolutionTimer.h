/*
 * The information in this file is
 * Copyright(c) 2007 Ball Aerospace & Technologies Corporation
 * and is subject to the terms and conditions of the
 * GNU Lesser General Public License Version 2.1
 * The license text is available from   
 * http://www.gnu.org/licenses/lgpl.html
 */

#ifndef HIGH_RESOLUTION_TIMER_H
#define HIGH_RESOLUTION_TIMER_H

#include "AppConfig.h"

#if defined(WIN_API)
#include "Windows.h"
#else
#include <sys/time.h>
#endif

#include <ostream>

namespace HrTimer
{

#if defined(WIN_API)
#define HrTimingType LONGLONG
#else
#define HrTimingType hrtime_t
#endif

inline HrTimingType getTime()
{
#if defined(WIN_API)
   LARGE_INTEGER currentTime;
   QueryPerformanceCounter(&currentTime);
   return currentTime.QuadPart;
#else
   return gethrtime();
#endif
}

inline double convertToSeconds(HrTimingType val)
{
#if defined(WIN_API)
   LARGE_INTEGER frequency;
   QueryPerformanceFrequency(&frequency);
   LONGLONG longFrequency = frequency.QuadPart;
   return val / ((double)longFrequency); 
#else
   return val / 1000000000.0; //val on Solaris is in nano-seconds
#endif
}

/**
 *  A class to perform high-resolution timing.
 *
 *  The Resource class allows simple computation of timing operations. The
 *  user simply creates a Resource object at the beginning of the operation
 *  to be timed, and when the object goes out of scope it will compute the
 *  elapsed time.
 */
class Resource
{
public:
   /**
    *  Creates a Resource object. When the object is destroyed, its total life
    *  span will be placed into the pOutputInto argument.
    *
    *  @param   pOutputInto
    *           A pointer to the number the object's lifespan should be put
    *           into
    *
    *  @param   milliSecond
    *           If true, the value will be computed in milliseconds, otherwise
    *           it will be in seconds.
    */
   Resource(double* pOutputInto, bool milliSecond = true) : 
      mStart(HrTimer::getTime()),
      mOutputType(2),
      mpDoubleOutput(pOutputInto),
      mpStartOutput(NULL),
      mpEndOutput(NULL),
      mMillisecondResolution(milliSecond)
   {
      mStart = HrTimer::getTime();
   }

   /**
    *  Creates a Resource object. When the object is destroyed, its creation 
    *  time will be placed into pStart and its destruction time will be placed 
    *  into pEnd.
    *
    *  @param   pStart
    *           A pointer to the value in which to place the construction time.
    *
    *  @param   pEnd
    *           A pointer to the value in which to place the destruction time.
    */
   Resource(HrTimingType* pStart, HrTimingType* pEnd) :
      mStart(HrTimer::getTime()),
      mOutputType(3),
      mpDoubleOutput(NULL),
      mpStartOutput(pStart),
      mpEndOutput(pEnd),
      mMillisecondResolution(false)
   {
   }

   /**
    *  Destroys a Resource object. 
    */
   ~Resource()
   {
      HrTimingType end = HrTimer::getTime();
      if (mOutputType == 2 && mpDoubleOutput!=NULL)
      {
         //output value into a double
         double timeDiff = convertToSeconds(end - mStart);
         if (mMillisecondResolution)
         {
            timeDiff *= 1000.0;
         }
         *mpDoubleOutput = timeDiff;
      }
      if (mOutputType == 3)
      {
         if (mpStartOutput!=NULL)
         {
            *mpStartOutput = mStart;
         }
         if (mpEndOutput!=NULL)
         {
            *mpEndOutput = end;
         }
      }
   }

private:
   HrTimingType mStart;
   int mOutputType;
   double* mpDoubleOutput;
   HrTimingType* mpStartOutput;
   HrTimingType* mpEndOutput;
   bool mMillisecondResolution;
};

};

#endif
