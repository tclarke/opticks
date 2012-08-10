/*
 * The information in this file is
 * Copyright(c) 2012 Ball Aerospace & Technologies Corporation
 * and is subject to the terms and conditions of the
 * GNU Lesser General Public License Version 2.1
 * The license text is available from   
 * http://www.gnu.org/licenses/lgpl.html
 */

#include "AppAssert.h"
#include "AppVerify.h"
#include "AppVersion.h"
#include "Executable.h"
#include "Filename.h"
#include "JSWizardExecutor.h"
#include "PlugInArgList.h"
#include "PlugInManagerServices.h"
#include "PlugInRegistration.h"
#include "Progress.h"

#include <v8.h>
#include <QtCore/QFile>

REGISTER_PLUGIN_BASIC(OpticksWizardExecutor, JSWizardExecutor);

JSWizardExecutor::JSWizardExecutor() :
   mbInteractive(false),
   mbAbort(false),
   mpPlugIn(NULL)
{
   setName("JavaScript Wizard Executor");
   setVersion(APP_VERSION_NUMBER);
   setCreator("Ball Aerospace & Technologies, Corp.");
   setCopyright(APP_COPYRIGHT);
   setShortDescription("JavaScript Wizard Executor");
   setDescription("JavaScript Wizard Executor based on the Google v8 engine.");
   setDescriptorId("{A7D78254-8564-40F2-8682-689958A58E17}");
   allowMultipleInstances(true);
   setProductionStatus(APP_IS_PRODUCTION_RELEASE);
}

JSWizardExecutor::~JSWizardExecutor()
{
}

bool JSWizardExecutor::setBatch()
{
   mbInteractive = false;
   return true;
}

bool JSWizardExecutor::setInteractive()
{
   mbInteractive = true;
   return true;
}

bool JSWizardExecutor::hasAbort()
{
   return (mpPlugIn == NULL) ? true : mpPlugIn->hasAbort();
}

bool JSWizardExecutor::getInputSpecification(PlugInArgList*& pArgList)
{
   VERIFY((pArgList = Service<PlugInManagerServices>()->getPlugInArgList()) != NULL);
   VERIFY(pArgList->addArg<Progress>(Executable::ProgressArg(), NULL, Executable::ProgressArgDescription()));
   VERIFY(pArgList->addArg<Filename>("Filename", ".jswiz file to be executed."));
   return true;
}

bool JSWizardExecutor::execute(PlugInArgList* pInArgList, PlugInArgList* pOutArgList)
{
   Progress* pProgress = pInArgList->getPlugInArgValue<Progress>(Executable::ProgressArg());
   Filename* pFilename = pInArgList->getPlugInArgValue<Filename>("Filename");
   if (pFilename == NULL)
   {
      if (pProgress) pProgress->updateProgress("No Filename specified.", 0, ERRORS);
      return false;
   } 
   QFile file(QString::fromStdString(pFilename->getFullPathAndName()));
   if (!file.open(QFile::ReadOnly | QFile::Text))
   {
      if (pProgress) pProgress->updateProgress("Unable to load jswiz file.", 0, ERRORS);
      return false;
   }
   QByteArray scriptBytes = file.readAll();

   // v8 code to execute the script
   v8::HandleScope handle_scope;
   v8::Persistent<v8::Context> context = v8::Context::New();
   v8::Context::Scope context_scope(context);

   v8::Handle<v8::String> scriptSource = v8::String::New(scriptBytes.data(), scriptBytes.size());
   v8::Handle<v8::Script> script = v8::Script::Compile(scriptSource);
   if (script.IsEmpty())
   {
      if (pProgress) pProgress->updateProgress("Unable to compile jswiz.", 0, ERRORS);
      return false;
   }
   v8::Handle<v8::Value> result = script->Run();

   // v8 cleanup code
   context.Dispose();

   if (pProgress)
   {
      v8::String::AsciiValue asciiResult(result);
      pProgress->updateProgress(*asciiResult, 100, NORMAL);
   }
   
   return true;
}

bool JSWizardExecutor::abort()
{
   mbAbort = true;
   return (mpPlugIn == NULL) ? true : mpPlugIn->abort();
}