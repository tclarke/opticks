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

v8::Handle<v8::Value> ConsoleCallback(v8::Local<v8::String> property, const v8::AccessorInfo& info)
{
   v8::Local<v8::Object> self = info.Holder();
   v8::Local<v8::External> wrap = v8::Local<v8::External>::Cast(self->GetInternalField(0));
   Progress* pProgress = static_cast<Progress*>(wrap->Value());
   /*v8::String::AsciiValue asciiValue(value);
   if (property->Equals(v8::String::New("log")))
   {
      std::string text;
      int percent;
      ReportingLevel gran;
      pProgress->getProgress(text, percent, gran);
      pProgress->updateProgress
   }
   else if (property->Equals(v8::String::New("warn")))
   {
   }
   else if (property->Equals(v8::String::New("error")))
   {
   }
   return v8::String::New(text.c_str());*/
   return v8::Null();
}

void SetProgress(v8::Local<v8::String> property, v8::Local<v8::Value> value, const v8::AccessorInfo& info)
{
   v8::Local<v8::Object> self = info.Holder();
   v8::Local<v8::External> wrap = v8::Local<v8::External>::Cast(self->GetInternalField(0));
   Progress* pProgress = static_cast<Progress*>(wrap->Value());
   v8::String::AsciiValue asciiValue(value);
   pProgress->updateProgress(std::string(*asciiValue), 10, NORMAL);
}

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
   v8::HandleScope hdlsc;
   v8::Persistent<v8::Context> context = v8::Context::New();
   v8::Context::Scope context_scope(context);

   v8::Handle<v8::ObjectTemplate> console_templ = v8::ObjectTemplate::New();
   console_templ->SetInternalFieldCount(1);

   /*v8::Handle<v8::FunctionTemplate> logc = v8::FunctionTemplate::New(ConsoleCallback);
   console_templ->Set(v8::String::New("log"), logc);
   v8::Handle<v8::FunctionTemplate> warnc = v8::FunctionTemplate::New(ConsoleCallback);
   console_templ->Set(v8::String::New("warn"), warnc);
   v8::Handle<v8::FunctionTemplate> errorc = v8::FunctionTemplate::New(ConsoleCallback);
   console_templ->Set(v8::String::New("error"), errorc);*/

   v8::Local<v8::Object> console = console_templ->NewInstance();
   console->SetInternalField(0, v8::External::New(pProgress));
   context->Global()->Set(v8::String::New("console"), console);

   v8::Handle<v8::String> scriptSource = v8::String::New(scriptBytes.data(), scriptBytes.size());
   v8::Handle<v8::Script> script = v8::Script::Compile(scriptSource, v8::String::New(pFilename->getFullPathAndName().c_str()));
   if (script.IsEmpty())
   {
      if (pProgress) pProgress->updateProgress("Unable to compile jswiz.", 0, ERRORS);
      return false;
   }
   v8::Handle<v8::Value> result = script->Run();

   // v8 cleanup code
   context.Dispose();

   if (pProgress && !result.IsEmpty())
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