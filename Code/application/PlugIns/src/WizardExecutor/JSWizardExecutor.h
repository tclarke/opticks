/*
 * The information in this file is
 * Copyright(c) 2012 Ball Aerospace & Technologies Corporation
 * and is subject to the terms and conditions of the
 * GNU Lesser General Public License Version 2.1
 * The license text is available from   
 * http://www.gnu.org/licenses/lgpl.html
 */

#ifndef JSWIZARDEXECUTOR_H
#define JSWIZARDEXECUTOR_H

#include "WizardShell.h"

class JSWizardExecutor : public WizardShell
{
public:
   JSWizardExecutor();
   virtual ~JSWizardExecutor();

   bool setBatch();
   bool setInteractive();
   bool hasAbort();
   bool getInputSpecification(PlugInArgList*& pArgList);
   bool execute(PlugInArgList* pInArgList, PlugInArgList* pOutArgList);
   bool abort();

private:
   bool mbInteractive;
   bool mbAbort;
   Executable* mpPlugIn;
};

#endif