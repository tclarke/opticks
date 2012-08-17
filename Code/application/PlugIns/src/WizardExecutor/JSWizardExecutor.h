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

#include "Interpreter.h"
#include "InterpreterManagerShell.h"
#include "SubjectImp.h"
#include <v8.h>

class JSInterpreter;

class JSWizardExecutor : public InterpreterManagerShell, SubjectImp
{
public:
   JSWizardExecutor();
   virtual ~JSWizardExecutor();

   virtual bool execute(PlugInArgList* pInArgList, PlugInArgList* pOutArgList);

   virtual bool isStarted() const;
   virtual bool start();
   virtual std::string getStartupMessage() const;
   virtual Interpreter* getInterpreter() const;

   virtual const std::string& getObjectType() const;
   virtual bool isKindOf(const std::string& className) const;

   SUBJECTADAPTER_METHODS(SubjectImp)

private:
   JSInterpreter* mpInterpreter;
};

class JSInterpreter : public Interpreter, public SubjectImp
{
public:
   JSInterpreter();
   virtual ~JSInterpreter();

   bool start();

   virtual std::string getPrompt() const;
   virtual bool executeCommand(const std::string& command);
   virtual bool executeScopedCommand(const std::string& command, const Slot& output, const Slot& error, Progress* pProgress);
   virtual bool isGlobalOutputShown() const;
   virtual void showGlobalOutput(bool val);

   virtual bool getLastResult() const;
   void sendOutput(const std::string& text);
   void sendError(const std::string& text);

   bool setArguments(PlugInArgList* pInArgList, PlugInArgList* pOutArgList);

   virtual const std::string& getObjectType() const;
   virtual bool isKindOf(const std::string& className) const;

   SUBJECTADAPTER_METHODS(SubjectImp)

private:
   void createGlobals();
   void sendOutput(const std::string& text, bool scoped);
   void sendError(const std::string& text, bool scoped);

   SIGNAL_METHOD(JSInterpreter, ScopedOutputText);
   SIGNAL_METHOD(JSInterpreter, ScopedErrorText);

   bool mGlobalOutputShown;
   v8::Handle<v8::ObjectTemplate> mGlobalTemplate;
   v8::Persistent<v8::Context> mMainContext;
   bool mIsScoped;
   bool mLastResult;
   PlugInArgList* mpInArgList;
   PlugInArgList* mpOutArgList;
};

#endif