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
#include "Subject.h"

#include <v8.h>
#include <QtCore/QFile>

REGISTER_PLUGIN_BASIC(OpticksWizardExecutor, JSWizardExecutor);

namespace
{
bool sFatalOccurred = false;
std::string sFatalMessage;

void handleFatalError(const char* pLocation, const char* pMessage)
{
   sFatalMessage = std::string(pMessage) + " at " + pLocation;
   sFatalOccurred = true;
}

#define GET_INTERP(args_) static_cast<JSInterpreter*>(v8::Local<v8::External>::Cast(v8::Handle<v8::Object>::Cast(args_.Holder()->CreationContext()->Global()->GetPrototype())->GetInternalField(0))->Value())

v8::Handle<v8::Value> send_out_callback(const v8::Arguments& args)
{
   JSInterpreter* pInterp = GET_INTERP(args);
   if (pInterp == NULL || args.Length() == 0)
   {
      return v8::Null();
   }
   std::string msg;
   for (int i = 0; i < args.Length(); ++i)
   {
      v8::String::AsciiValue asciiValue(args[i]);
      if (!msg.empty())
      {
         msg += " ";
      }
      msg += *asciiValue;
   }
   pInterp->sendOutput(msg);
   return v8::Null();
}

v8::Handle<v8::Value> send_error_callback(const v8::Arguments& args)
{
   JSInterpreter* pInterp = GET_INTERP(args);
   if (pInterp == NULL || args.Length() == 0)
   {
      return v8::Null();
   }
   std::string msg;
   for (int i = 0; i < args.Length(); ++i)
   {
      v8::String::AsciiValue asciiValue(args[i]);
      if (!msg.empty())
      {
         msg += " ";
      }
      msg += *asciiValue;
   }
   pInterp->sendError(msg);
   return v8::Null();
}
}

JSWizardExecutor::JSWizardExecutor() : mpInterpreter(NULL)
{
   setName("Javascript");
   setVersion(APP_VERSION_NUMBER);
   setCreator("Ball Aerospace & Technologies, Corp.");
   setCopyright(APP_COPYRIGHT);
   setShortDescription("Javascript interpreter manager.");
   setDescriptorId("{A7D78254-8564-40F2-8682-689958A58E17}");
   allowMultipleInstances(false);
   setProductionStatus(APP_IS_PRODUCTION_RELEASE);
   setFileExtensions("Javascript Files (*.js)");
   setWizardSupported(false);
   setInteractiveEnabled(true);
   addMimeType("text/javascript");
}

JSWizardExecutor::~JSWizardExecutor()
{
}

bool JSWizardExecutor::execute(PlugInArgList* pInArgList, PlugInArgList* pOutArgList)
{
   start();
   return true;
}

bool JSWizardExecutor::isStarted() const
{
   return mpInterpreter != NULL && !sFatalOccurred;
}

bool JSWizardExecutor::start()
{
   mpInterpreter = new JSInterpreter();
   if (sFatalOccurred)
   {
      return false;
   }
   return mpInterpreter->start();
}

std::string JSWizardExecutor::getStartupMessage() const
{
   if (sFatalOccurred)
   {
      return sFatalMessage;
   }
   if (mpInterpreter == NULL)
   {
      return "v8 javascript engine could not be initialized";
   }
   return std::string("v8 javascript engine version ") + v8::V8::GetVersion();
}

Interpreter* JSWizardExecutor::getInterpreter() const
{
   return mpInterpreter;
}

const std::string& JSWizardExecutor::getObjectType() const
{
   static std::string sType("JSWizardExecutor");
   return sType;
}

bool JSWizardExecutor::isKindOf(const std::string& className) const
{
   if (className == getObjectType())
   {
      return true;
   }
   return SubjectImp::isKindOf(className);
}

JSInterpreter::JSInterpreter() : mGlobalOutputShown(false), mIsScoped(false), mLastResult(false)
{
}

JSInterpreter::~JSInterpreter()
{
   if (!mMainContext.IsEmpty())
   {
      mMainContext.Dispose();
   }
}

bool JSInterpreter::start()
{
   v8::V8::SetFatalErrorHandler(handleFatalError);
   v8::HandleScope hdlsc;
   createGlobals();
   mMainContext  = v8::Context::New(NULL, mGlobalTemplate);
   v8::Context::Scope context_scope(mMainContext);
   v8::Handle<v8::Object>::Cast(mMainContext->Global()->GetPrototype())->SetInternalField(0, v8::External::New(this));
   return true;
}

std::string JSInterpreter::getPrompt() const
{
   return "> ";
}

bool JSInterpreter::executeCommand(const std::string& command)
{
   v8::HandleScope hdlsc;
   v8::Context::Scope context_scope(mMainContext);

   v8::Handle<v8::String> scriptSource = v8::String::New(command.c_str());
   v8::Handle<v8::Script> script = v8::Script::Compile(scriptSource);
   if (script.IsEmpty())
   {
      return false;
   }
   bool rval = true;
   v8::TryCatch trycatch;
   v8::Handle<v8::Value> result = script->Run();
   if (result.IsEmpty())
   {
      v8::Handle<v8::Value> exception = trycatch.Exception();
      v8::String::AsciiValue exc_str(exception);
      sendError(*exc_str, false);
      rval = false;
      mLastResult = false;
   }
   else
   {
      mLastResult = result->BooleanValue();
   }
   return rval;
}

bool JSInterpreter::executeScopedCommand(const std::string& command, const Slot& output, const Slot& error, Progress* pProgress)
{
   attach(SIGNAL_NAME(JSInterpreter, ScopedOutputText), output);
   attach(SIGNAL_NAME(JSInterpreter, ScopedErrorText), error);
   mIsScoped = true;

   v8::HandleScope hdlsc;
   v8::Persistent<v8::Context> context = v8::Context::New(NULL, mGlobalTemplate);
   v8::Context::Scope context_scope(context);
   context->Global()->SetInternalField(0, v8::External::New(this));

   v8::Handle<v8::String> scriptSource = v8::String::New(command.c_str());
   v8::Handle<v8::Script> script = v8::Script::Compile(scriptSource);
   if (script.IsEmpty())
   {
      context.Dispose();
      mIsScoped = false;
      detach(SIGNAL_NAME(JSInterpreter, ScopedOutputText), output);
      detach(SIGNAL_NAME(JSInterpreter, ScopedErrorText), error);
      return false;
   }
   v8::TryCatch trycatch;
   v8::Handle<v8::Value> result = script->Run();
   bool rval = true;
   if (result.IsEmpty())
   {
      v8::Handle<v8::Value> exception = trycatch.Exception();
      v8::String::AsciiValue exc_str(exception);
      sendError(*exc_str, true);
      rval = false;
      mLastResult = false;
   }
   else
   {
      mLastResult = result->BooleanValue();
   }
   context.Dispose();
   mIsScoped = false;
   detach(SIGNAL_NAME(JSInterpreter, ScopedOutputText), output);
   detach(SIGNAL_NAME(JSInterpreter, ScopedErrorText), error);
   return rval;
}

bool JSInterpreter::isGlobalOutputShown() const
{
   return mGlobalOutputShown;
}

void JSInterpreter::showGlobalOutput(bool val)
{
   mGlobalOutputShown = true;
}

bool JSInterpreter::getLastResult() const
{
   return mLastResult;
}

void JSInterpreter::sendOutput(const std::string& text)
{
   return sendOutput(text, mIsScoped);
}

void JSInterpreter::sendError(const std::string& text)
{
   return sendError(text, mIsScoped);
}

const std::string& JSInterpreter::getObjectType() const
{
   static std::string sType("JSInterpreter");
   return sType;
}

bool JSInterpreter::isKindOf(const std::string& className) const
{
   if (className == getObjectType())
   {
      return true;
   }
   return SubjectImp::isKindOf(className);
}

void JSInterpreter::createGlobals()
{
   mGlobalTemplate = v8::ObjectTemplate::New();
   // We'll set a single internal field to hold the JSInterpreter this pointer
   // This needs to be set in each new context like this:
   // context->Global()->SetInternalField(0, v8::External::New(this));
   mGlobalTemplate->SetInternalFieldCount(1);

   // todo: replace this with .js files from node.js
   v8::Local<v8::ObjectTemplate> console_template = v8::ObjectTemplate::New();
   console_template->Set("log", v8::FunctionTemplate::New(send_out_callback));
   console_template->Set("debug", v8::FunctionTemplate::New(send_out_callback));
   console_template->Set("info", v8::FunctionTemplate::New(send_out_callback));
   console_template->Set("warn", v8::FunctionTemplate::New(send_out_callback));
   console_template->Set("error", v8::FunctionTemplate::New(send_error_callback));
   mGlobalTemplate->Set("console", console_template);
}

void JSInterpreter::sendOutput(const std::string& text, bool scoped)
{
   if (text.empty())
   {
      return;
   }
   if (scoped)
   {
      notify(SIGNAL_NAME(JSInterpreter, ScopedOutputText), text);
   }
   if (!scoped || mGlobalOutputShown)
   {
      notify(SIGNAL_NAME(Interpreter, OutputText), text);
   }
}

void JSInterpreter::sendError(const std::string& text, bool scoped)
{
   if (text.empty())
   {
      return;
   }
   if (scoped)
   {
      notify(SIGNAL_NAME(JSInterpreter, ScopedErrorText), text);
   }
   if (!scoped || mGlobalOutputShown)
   {
      notify(SIGNAL_NAME(Interpreter, ErrorText), text);
   }
}

#if 0
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
#endif