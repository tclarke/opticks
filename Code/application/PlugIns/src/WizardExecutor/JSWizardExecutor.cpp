/*
 * The information in this file is
 * Copyright(c) 2012 Ball Aerospace & Technologies Corporation
 * and is subject to the terms and conditions of the
 * GNU Lesser General Public License Version 2.1
 * The license text is available from   
 * http://www.gnu.org/licenses/lgpl.html
 */

#include "AppVerify.h"
#include "AppVersion.h"
#include "DateTime.h"
#include "Executable.h"
#include "Filename.h"
#include "Int64.h"
#include "JSWizardExecutor.h"
#include "PlugInArg.h"
#include "PlugInArgList.h"
#include "PlugInDescriptor.h"
#include "PlugInManagerServices.h"
#include "PlugInRegistration.h"
#include "PlugInResource.h"
#include "Progress.h"
#include "StringUtilities.h"
#include "Subject.h"
#include "UInt64.h"

#include <v8.h>
#include <QtCore/QDir>
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

template<typename T>
inline T* unwrap(const v8::AccessorInfo& info, unsigned int f=0)
{
   return static_cast<T*>(info.Holder()->GetPointerFromInternalField(f));
}

inline JSInterpreter* unwrap(const v8::Arguments& args, unsigned int f=0)
{
   return static_cast<JSInterpreter*>(v8::Handle<v8::Object>::Cast( \
      args.Holder()->CreationContext()->Global()->GetPrototype())->GetPointerFromInternalField(0));
}

v8::Handle<v8::Value> send_out_callback(const v8::Arguments& args)
{
   JSInterpreter* pInterp = unwrap(args);
   if (pInterp == NULL)
   {
      return v8::ThrowException(v8::String::New("Fatal error: Unable to locate interpreter handle."));
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
   JSInterpreter* pInterp = unwrap(args);
   if (pInterp == NULL)
   {
      return v8::ThrowException(v8::String::New("Fatal error: Unable to locate interpreter handle."));
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

v8::Handle<v8::Value> require(const v8::Arguments& args)
{
   if (args.Length() != 1)
   {
      return v8::ThrowException(v8::Exception::Error(v8::String::New("Incorrect number of arguments. require('module_name')")));
   }
   v8::HandleScope sc;

   v8::Local<v8::Value> cur = args.Holder()->Get(args[0]);
   if (!cur->IsUndefined())
   {
      return sc.Close(cur);
   }
   QDir d(Service<ConfigurationSettings>()->getSettingSupportFilesPath()->getFullPathAndName().c_str());
   d.cd("v8");
   v8::String::AsciiValue ascii(args[0]);
   QString fname = QString("%1.js").arg(*ascii);
   // for security, we explicitly check the file entry list
   // so someone can't do require('/bad/path/file')
   if (!d.entryList(QDir::Files).contains(fname))
   {
      return v8::ThrowException(v8::Exception::Error(v8::String::New("Module not found. Modules must be in SupportFiles/v8.")));
   }

   QFile module(d.absoluteFilePath(fname));
   if (!module.open(QFile::ReadOnly | QFile::Text))
   {
      return v8::ThrowException(v8::Exception::Error(v8::String::New("Unable to load module.")));
   }
   QByteArray bytes = module.readAll();

   v8::Handle<v8::Object> exports = v8::Object::New();
   exports->Set(v8::String::New("_name"), args[0]);
   exports->Set(v8::String::New("_file"), v8::String::New(fname.toAscii().constData()));
   v8::Local<v8::String> exports_name = v8::String::New("exports");
   v8::Local<v8::Value> tmp_exports = args.Holder()->Get(exports_name);
   args.Holder()->Set(exports_name, exports);

   v8::Handle<v8::String> scriptSource = v8::String::New(bytes.constData());
   v8::Handle<v8::Script> script = v8::Script::Compile(scriptSource, v8::String::New(fname.toAscii().constData()));
   if (script.IsEmpty())
   {
      return v8::ThrowException(v8::Exception::Error(v8::String::New("Unable to load module.")));
   }
   v8::TryCatch trycatch;

   v8::Handle<v8::Value> result = script->Run();
   if (tmp_exports->IsUndefined())
   {
      args.Holder()->Delete(exports_name);
   }
   else
   {
      args.Holder()->Set(exports_name, tmp_exports);
   }
   if (result.IsEmpty())
   {
      v8::Handle<v8::Value> exception = trycatch.Exception();
      return v8::ThrowException(exception);
   }
   return sc.Close(exports);
}

#define CVT(typ_, jsc_) if (arg.getType() == #typ_) return jsc_(*(arg.getPlugInArgValue<typ_>()));
#define CVTA(typ_, jsc_) CVT(typ_, jsc_) \
   if (arg.getType() == "vector<"#typ_">") { \
   std::vector<typ_>& v(*(arg.getPlugInArgValue<std::vector<typ_> >())); \
   v8::HandleScope sc; \
   v8::Handle<v8::Array> a = v8::Array::New(v.size()); \
   if (a.IsEmpty()) return v8::Handle<v8::Array>(); \
   for (unsigned int i = 0; i < v.size(); ++i) a->Set(i, jsc_(v[i])); \
   return sc.Close(a); }
#define CVTE(typ_) if (arg.getType() == #typ_){ \
   v8::Handle<v8::ObjectTemplate> t = v8::ObjectTemplate::New(); \
   t->SetInternalFieldCount(1); \
   v8::Handle<v8::Object> o = t->NewInstance(); \
   o->SetPointerInInternalField(0, arg.isActualSet() ? arg.getActualValue() : arg.getDefaultValue()); \
   o->Set(v8::String::New("type"), v8::String::New(arg.getType().c_str())); \
   return o; }

v8::Handle<v8::Value> plugInArgToJs(PlugInArg& arg)
{
   if (!arg.isActualSet() && !arg.isDefaultSet()) return v8::Undefined();
   CVTA(char, v8::Int32::New)
   CVTA(signed char, v8::Int32::New)
   CVTA(unsigned char, v8::Uint32::New)
   CVTA(short, v8::Int32::New)
   CVTA(unsigned short, v8::Uint32::New)
   CVTA(int, v8::Int32::New)
   CVTA(unsigned int, v8::Uint32::New)
   CVTA(long, v8::Int32::New)
   CVTA(unsigned long, v8::Uint32::New)
   CVTA(Int64, v8::Integer::New)
   CVTA(int64_t, v8::Integer::New)
   CVTA(UInt64, v8::Integer::NewFromUnsigned)
   CVTA(uint64_t, v8::Integer::NewFromUnsigned)
   CVTA(float, v8::Number::New)
   CVTA(double, v8::Number::New)
   CVTA(bool, v8::Boolean::New)
   if (arg.getType() == "string") return v8::String::New(arg.getPlugInArgValue<std::string>()->c_str());
   if (arg.getType() == "vector<string>")
   {
      std::vector<std::string>& v(*(arg.getPlugInArgValue<std::vector<std::string> >()));
      v8::HandleScope sc;
      v8::Handle<v8::Array> a = v8::Array::New(v.size());
      if (a.IsEmpty()) return v8::Handle<v8::Array>();
      for (unsigned int i = 0; i < v.size(); ++i) a->Set(i, v8::String::New(v[i].c_str()));
      return sc.Close(a);
   }
   if (arg.getType() == "Filename") return v8::String::New(arg.getPlugInArgValue<Filename>()->getFullPathAndName().c_str());
   if (arg.getType() == "vector<Filename>")
   {
      std::vector<Filename*>& v(*(arg.getPlugInArgValue<std::vector<Filename*> >()));
      v8::HandleScope sc;
      v8::Handle<v8::Array> a = v8::Array::New(v.size());
      if (a.IsEmpty()) return v8::Handle<v8::Array>();
      for (unsigned int i = 0; i < v.size(); ++i)
      {
         std::string fname = v[i]->getFullPathAndName();
         a->Set(i, v8::String::New(fname.c_str()));
      }
      return sc.Close(a);
   }
   CVTE(Animation)
   CVTE(AnimationController)
   CVTE(AnnotationElement)
   CVTE(AnnotationLayer)
   CVTE(Any)
   CVTE(AoiElement)
   CVTE(AoiLayer)
   CVTE(CartesianPlot)
   CVTE(ClassificationLayer)
   CVTE(CustomLayer)
   CVTE(DataDescriptor)
   CVTE(DataElement)
   if (arg.getType() == "DateTime")
   {
      DateTime* pVal = arg.getPlugInArgValue<DateTime>();
      if (!pVal->isValid()) return v8::Undefined();
      return v8::Date::New((double)pVal->getStructured());
   }
   CVTE(DynamicObject)
   CVTE(FileDescriptor)
   CVTE(GcpLayer)
   CVTE(GcpList)
   CVTE(GraphicElement)
   CVTE(GraphicLayer)
   CVTE(HistogramPlot)
   CVTE(LatLonLayer)
   CVTE(Layer)
   CVTE(MeasurementLayer)
   CVTE(OrthographicView)
   CVTE(PerspectiveView)
   CVTE(PlotView)
   CVTE(PlotWidget)
   CVTE(PolarPlot)
   CVTE(ProductView)
   CVTE(ProductWindow)
   CVTE(Progress)
   CVTE(PseudocolorLayer)
   CVTE(RasterDataDescriptor)
   CVTE(RasterElement)
   CVTE(RasterFileDescriptor)
   CVTE(RasterLayer)
   CVTE(Signature)
   CVTE(SignatureDataDescriptor)
   CVTE(SignatureFileDescriptor)
   CVTE(SignatureLibrary)
   CVTE(SignaturePlot)
   CVTE(SignatureSet)
   CVTE(SpatialDataView)
   CVTE(SpatialDataWindow)
   CVTE(ThresholdLayer)
   CVTE(TiePointList)
   CVTE(TiePointLayer)
   CVTE(View)
   CVTE(Wavelengths)
   CVTE(Window)
   CVTE(WizardObject)

   // fall through...convert to an XML string
   DataVariant v(arg.getType(), arg.isActualSet() ? arg.getActualValue() : arg.getDefaultValue());
   if (!v.isValid()) return v8::Undefined();
   return v8::String::New(v.toXmlString().c_str());
}

#define CVTF(typ_, conv_) if (arg.getType() == #typ_) { typ_ v(val->conv_); return argList.setPlugInArgValue(name, &v); }
#define CVTFA(typ_, conv_) CVTF(typ_, conv_) \
   if (arg.getType() == "vector<"#typ_">") { \
   v8::Handle<v8::Array> a = v8::Handle<v8::Array>::Cast(val); \
   std::vector<typ_> v; \
   v.reserve(a->Length()); \
   for (unsigned int i = 0; i < a->Length(); ++i) v.push_back(typ_(a->Get(i)->conv_)); \
   return argList.setPlugInArgValue(name, &v); }
#define CVTFE(typ_) if (arg.getType() == #typ_) {  v8::Local<v8::Object> obj = val->ToObject(); \
      v8::String::AsciiValue ascii(obj->Get(v8::String::New("type"))->ToString()); \
      if (obj->InternalFieldCount() == 0 || std::string(*ascii) != #typ_) return false; \
      arg.setActualValue(obj->GetPointerFromInternalField(0), false); \
      return true; }

bool plugInArgFromJs(PlugInArg& arg, PlugInArgList& argList, const std::string& name, v8::Handle<v8::Value> val)
{
   v8::HandleScope sc;
   CVTFA(char, ToInt32()->Value())
   CVTFA(signed char, ToInt32()->Value())
   CVTFA(unsigned char, ToUint32()->Value())
   CVTFA(short, ToInt32()->Value())
   CVTFA(unsigned short, ToUint32()->Value())
   CVTFA(int, ToInt32()->Value())
   CVTFA(unsigned int, ToUint32()->Value())
   CVTFA(long, ToInt32()->Value())
   CVTFA(unsigned long, ToUint32()->Value())
   CVTFA(Int64, ToInteger()->Value())
   CVTFA(int64_t, ToInteger()->Value())
   CVTFA(UInt64, ToInteger()->Value())
   CVTFA(uint64_t, ToInteger()->Value())
   CVTFA(float, ToNumber()->Value())
   CVTFA(double, ToNumber()->Value())
   CVTFA(bool, ToBoolean()->Value())
   if (arg.getType() == "string")
   {
      v8::String::AsciiValue a(val->ToString());
      std::string st(*a);
      return argList.setPlugInArgValue(name, &st);
   }
   if (arg.getType() == "vector<string>")
   {
      v8::Handle<v8::Array> a = v8::Handle<v8::Array>::Cast(val);
      std::vector<std::string> v;
      v.reserve(a->Length());
      for (unsigned int i = 0; i < a->Length(); ++i)
      {
         v8::String::AsciiValue ascii(a->Get(i)->ToString());
         v.push_back(std::string(*ascii));
      }
      return argList.setPlugInArgValue(name, &v);
   }
   if (arg.getType() == "Filename")
   {
      v8::String::AsciiValue a(val->ToString());
      FactoryResource<Filename> fname;
      fname->setFullPathAndName(*a);
      return argList.setPlugInArgValue(name, fname.get());
   }
   if (arg.getType() == "vector<Filename>")
   {
      v8::Handle<v8::Array> a = v8::Handle<v8::Array>::Cast(val);
      std::vector<Filename*> v;
      v.reserve(a->Length());
      for (unsigned int i = 0; i < a->Length(); ++i)
      {
         v8::String::AsciiValue ascii(a->Get(i)->ToString());
         FactoryResource<Filename> fname;
         fname->setFullPathAndName(*ascii);
         v.push_back(fname.release());
      }
      return argList.setPlugInArgValue(name, &v);
   }
   CVTFE(Animation)
   CVTFE(AnimationController)
   CVTFE(AnnotationElement)
   CVTFE(AnnotationLayer)
   CVTFE(Any)
   CVTFE(AoiElement)
   CVTFE(AoiLayer)
   CVTFE(CartesianPlot)
   CVTFE(ClassificationLayer)
   CVTFE(CustomLayer)
   CVTFE(DataDescriptor)
   CVTFE(DataElement)
   if (arg.getType() == "DateTime")
   {
      double nv = v8::Handle<v8::Date>::Cast(val)->NumberValue();
      FactoryResource<DateTime> dt;
      dt->setStructured((time_t)nv);
      return argList.setPlugInArgValue(name, dt.get());
   }
   CVTFE(DynamicObject)
   CVTFE(FileDescriptor)
   CVTFE(GcpLayer)
   CVTFE(GcpList)
   CVTFE(GraphicElement)
   CVTFE(GraphicLayer)
   CVTFE(HistogramPlot)
   CVTFE(LatLonLayer)
   CVTFE(Layer)
   CVTFE(MeasurementLayer)
   CVTFE(OrthographicView)
   CVTFE(PerspectiveView)
   CVTFE(PlotView)
   CVTFE(PlotWidget)
   CVTFE(PolarPlot)
   CVTFE(ProductView)
   CVTFE(ProductWindow)
   CVTFE(Progress)
   CVTFE(PseudocolorLayer)
   CVTFE(RasterDataDescriptor)
   CVTFE(RasterElement)
   CVTFE(RasterFileDescriptor)
   CVTFE(RasterLayer)
   CVTFE(Signature)
   CVTFE(SignatureDataDescriptor)
   CVTFE(SignatureFileDescriptor)
   CVTFE(SignatureLibrary)
   CVTFE(SignaturePlot)
   CVTFE(SignatureSet)
   CVTFE(SpatialDataView)
   CVTFE(SpatialDataWindow)
   CVTFE(ThresholdLayer)
   CVTFE(TiePointList)
   CVTFE(TiePointLayer)
   CVTFE(View)
   CVTFE(Wavelengths)
   CVTFE(Window)
   CVTFE(WizardObject)

   // fall through...convert to an XML string
   v8::String::AsciiValue ascii(val->ToString());
   DataVariant v;
   if (v.fromXmlString(arg.getType(), *ascii) == DataVariant::FAILURE)
   {
      return false;
   }
   arg.setActualValue(v.getPointerToValueAsVoid());
   return true;
}
v8::Handle<v8::Value> plugInArgListGetter(v8::Local<v8::String> name, const v8::AccessorInfo& info)
{
   if (name->Equals(v8::String::NewSymbol("inspect")))
   {
      return v8::Undefined();
   }
   PlugInArgList* pPial = unwrap<PlugInArgList>(info);
   if (pPial == NULL)
   {
      return v8::ThrowException(v8::String::New("Error: Unable to locate the PlugInArgList handle."));
   }
   PlugInArg* pArg;
   if (!pPial->getArg(*v8::String::AsciiValue(name), pArg) || pArg == NULL)
   {
      return v8::ThrowException(v8::Exception::ReferenceError(v8::String::New("Plug-in argument not found.")));
   }
   return plugInArgToJs(*pArg);
}

v8::Handle<v8::Value> plugInArgListSetter(v8::Local<v8::String> name, v8::Local<v8::Value> value, const v8::AccessorInfo& info)
{
   PlugInArgList* pPial = unwrap<PlugInArgList>(info);
   if (pPial == NULL)
   {
      return v8::ThrowException(v8::String::New("Error: Unable to locate the PlugInArgList handle."));
   }
   std::string nm(*v8::String::AsciiValue(name));
   PlugInArg* pArg;
   if (!pPial->getArg(nm, pArg) || pArg == NULL)
   {
      return v8::ThrowException(v8::Exception::ReferenceError(v8::String::New("Plug-in argument not found.")));
   }
   if (!plugInArgFromJs(*pArg, *pPial, nm, value))
   {
      return v8::ThrowException(v8::Exception::TypeError(v8::String::New("Unable to convert data")));
   }
   return v8::Null();
}
v8::Handle<v8::Array> plugInArgListEnumerator(const v8::AccessorInfo& info)
{
   PlugInArgList* pPial = unwrap<PlugInArgList>(info);
   if (pPial == NULL)
   {
      return v8::Handle<v8::Array>();
   }
   unsigned int cnt = pPial->getCount();
   v8::HandleScope sc;
   v8::Handle<v8::Array> a = v8::Array::New(cnt);
   if (a.IsEmpty()) return v8::Handle<v8::Array>();
   for (unsigned int i = 0; i < cnt; ++i)
   {
      PlugInArg* pArg;
      if (!pPial->getArg(i, pArg) || pArg == NULL)
      {
         return v8::Handle<v8::Array>();
      }
      a->Set(i, v8::String::New(pArg->getName().c_str()));
   }
   return sc.Close(a);
}

void cleanup_plugin(v8::Persistent<v8::Value> obj, void* pInterpPtr)
{
   JSInterpreter* pInterp = static_cast<JSInterpreter*>(pInterpPtr);
   VERIFYNRV(pInterp);
   std::list<v8::Persistent<v8::Object> >::iterator it = std::find(pInterp->mTrackedObjects.begin(), pInterp->mTrackedObjects.end(), obj);
   if (it != pInterp->mTrackedObjects.end())
   {
      pInterp->mTrackedObjects.erase(it);
   }
   delete static_cast<ExecutableResource*>(v8::Handle<v8::Object>::Cast(obj)->GetPointerFromInternalField(0));
}

v8::Handle<v8::Value> free_plugin(const v8::Arguments& args)
{
   ExecutableResource* pPlugIn = static_cast<ExecutableResource*>(args.This()->GetPointerFromInternalField(0));
   if (pPlugIn == NULL)
   {
      return v8::ThrowException(v8::Exception::Error(v8::String::New("This plug-in has been forcibly freed and is no longer valid.")));
   }
   delete pPlugIn;
   args.This()->SetPointerInInternalField(0, NULL);
   v8::Local<v8::Object>::Cast(args.This()->Get(v8::String::New("input")))->SetPointerInInternalField(0, NULL);
   v8::Local<v8::Object>::Cast(args.This()->Get(v8::String::New("output")))->SetPointerInInternalField(0, NULL);
   args.This()->Delete(v8::String::New("input"));
   args.This()->Delete(v8::String::New("output"));
   return v8::Undefined();
}

v8::Handle<v8::Value> exec_plugin(const v8::Arguments& args)
{
   ExecutableResource* pPlugIn = static_cast<ExecutableResource*>(args.This()->GetPointerFromInternalField(0));
   if (pPlugIn == NULL)
   {
      return v8::ThrowException(v8::Exception::Error(v8::String::New("This plug-in has been forcibly freed and is no longer valid.")));
   }
   return v8::Boolean::New((*pPlugIn)->execute());
}

v8::Handle<v8::Value> create_plugin(const v8::Arguments& args)
{
   JSInterpreter* pInterp = unwrap(args);
   if (pInterp == NULL)
   {
      return v8::ThrowException(v8::String::New("Error accessing Javascript interpreter object."));
   }
   v8::HandleScope sc;
   if (args.Length() < 1 || args.Length() > 2)
   {
      return v8::ThrowException(v8::Exception::Error(v8::String::New("Must specify the name of the plug-in to create and an optional bool indicating batch mode.")));
   }
   v8::String::AsciiValue plugin_name(args[0]);
   bool batch = true;
   if (args.Length() > 1)
   {
      batch = args[1]->BooleanValue();
   }
   Progress* pProgress = static_cast<Progress*>(v8::Handle<v8::Object>::Cast( \
      args.Holder()->CreationContext()->Global()->GetPrototype())->GetPointerFromInternalField(1));

   ExecutableResource* pPlugIn = new ExecutableResource(*plugin_name, std::string(), pProgress, batch);
   if ((*pPlugIn)->getPlugIn() == NULL)
   {
      delete pPlugIn;
      return v8::ThrowException(v8::Exception::Error(v8::String::New("Unable to create requested plugin.")));
   }
   (*pPlugIn)->setAutoArg(false);

   v8::Local<v8::ObjectTemplate> plugin_template = v8::ObjectTemplate::New();
   plugin_template->SetInternalFieldCount(1);
   plugin_template->SetCallAsFunctionHandler(create_plugin);
   v8::Local<v8::ObjectTemplate> pial_template = v8::ObjectTemplate::New();
   pial_template->SetInternalFieldCount(1);
   pial_template->SetNamedPropertyHandler(plugInArgListGetter, plugInArgListSetter, NULL, NULL, plugInArgListEnumerator);
   plugin_template->Set("execute", v8::FunctionTemplate::New(exec_plugin));
   plugin_template->Set("input", pial_template);
   plugin_template->Set("output", pial_template);
   plugin_template->Set("free", v8::FunctionTemplate::New(free_plugin));

   v8::Local<v8::Object> obj = plugin_template->NewInstance();
   obj->SetPointerInInternalField(0, pPlugIn);

   obj->Set(v8::String::New("name"), args[0]);

   v8::Local<v8::Object>::Cast(obj->Get(v8::String::New("input")))->SetPointerInInternalField(
         0, &(*pPlugIn)->getInArgList());
   v8::Local<v8::Object>::Cast(obj->Get(v8::String::New("output")))->SetPointerInInternalField(
         0, &(*pPlugIn)->getOutArgList());

   v8::Persistent<v8::Object> pers(obj);
   pers.MakeWeak(pInterp, cleanup_plugin);
   pInterp->mTrackedObjects.push_back(pers);
   return sc.Close(obj);
}

v8::Handle<v8::Array> plugins_enumerator(const v8::AccessorInfo& info)
{
   std::vector<PlugInDescriptor*> desc = Service<PlugInManagerServices>()->getPlugInDescriptors();
   std::vector<std::string> names;
   for (std::vector<PlugInDescriptor*>::const_iterator it = desc.begin(); it != desc.end(); ++it)
   {
      if ((*it)->hasWizardSupport())
      {
         names.push_back((*it)->getName());
      }
   }
   v8::HandleScope sc;
   v8::Handle<v8::Array> a = v8::Array::New(names.size());
   if (a.IsEmpty()) return v8::Handle<v8::Array>();
   for (unsigned int i = 0; i < names.size(); ++i)
   {
      a->Set(i, v8::String::New(names[i].c_str()));
   }
   return sc.Close(a);
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

JSInterpreter::JSInterpreter() :
      mGlobalOutputShown(false),
      mIsScoped(false),
      mLastResult(false),
      mpInArgList(NULL),
      mpOutArgList(NULL)
{
}

JSInterpreter::~JSInterpreter()
{
   if (!mMainContext.IsEmpty())
   {
      while (!mTrackedObjects.empty())
      {
         delete static_cast<ExecutableResource*>(mTrackedObjects.front()->GetPointerFromInternalField(0));
         mTrackedObjects.pop_front();
      }
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
   v8::Handle<v8::Object>::Cast(mMainContext->Global()->GetPrototype())->SetPointerInInternalField(0, this);

   /* load system level code */
   QString fname((Service<ConfigurationSettings>()->getSettingSupportFilesPath()->getFullPathAndName() + "/v8/init.js").c_str());
   QFile init(fname);
   if (!init.open(QFile::ReadOnly | QFile::Text))
   {
      return false;
   }
   QByteArray bytes = init.readAll();
   v8::Handle<v8::String> scriptSource = v8::String::New(bytes.constData());
   v8::Handle<v8::Script> script = v8::Script::Compile(scriptSource, v8::String::New(fname.toAscii().constData()));
   if (script.IsEmpty())
   {
      return false;
   }
   v8::TryCatch trycatch;
   v8::Handle<v8::Value> result = script->Run();
   if (result.IsEmpty())
   {
      v8::Handle<v8::Value> exception = trycatch.Exception();
      v8::String::AsciiValue exc_str(exception);
      std::string errMsg = *exc_str;
      v8::Handle<v8::Message> msg = trycatch.Message();
      if (!msg.IsEmpty())
      {
         errMsg = *v8::String::AsciiValue(msg->GetScriptResourceName());
         errMsg += ":" + StringUtilities::toDisplayString(msg->GetLineNumber()) + ": ";
         errMsg += *exc_str;
      }
      sendError(errMsg, false);
      return false;
   }
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
      if (!result->IsNull() && !result->IsUndefined() && !result->IsExternal())
      {
         v8::String::AsciiValue a(result->ToString());
         sendOutput(*a);
      }
   }
   return rval;
}

bool JSInterpreter::executeScopedCommand(const std::string& command, const Slot& output, const Slot& error, Progress* pProgress)
{
   attach(SIGNAL_NAME(JSInterpreter, ScopedOutputText), output);
   attach(SIGNAL_NAME(JSInterpreter, ScopedErrorText), error);
   mIsScoped = true;

   v8::HandleScope hdlsc;
   //v8::Persistent<v8::Context> context = v8::Context::New(NULL, mGlobalTemplate);
   //v8::Context::Scope context_scope(context);
   v8::Context::Scope context_scope(mMainContext);
   v8::Handle<v8::Object>::Cast(mMainContext->Global()->GetPrototype())->SetPointerInInternalField(1, pProgress);
   //context->Global()->SetInternalField(0, v8::External::New(this));

   // set in/out arguments
   v8::Local<v8::ObjectTemplate> pial_template = v8::ObjectTemplate::New();
   pial_template->SetInternalFieldCount(1);
   pial_template->SetNamedPropertyHandler(plugInArgListGetter, plugInArgListSetter, NULL, NULL, plugInArgListEnumerator);
   if (mpInArgList != NULL)
   {
      v8::Local<v8::Object> pial_in = pial_template->NewInstance();
      pial_in->SetPointerInInternalField(0, mpInArgList);
      mMainContext->Global()->Set(v8::String::New("input"), pial_in);
   }
   if (mpOutArgList != NULL)
   {
      v8::Local<v8::Object> pial_out = pial_template->NewInstance();
      pial_out->SetPointerInInternalField(0, mpOutArgList);
      mMainContext->Global()->Set(v8::String::New("output"), pial_out);
   }

   v8::Handle<v8::String> scriptSource = v8::String::New(command.c_str());
   v8::Handle<v8::Script> script = v8::Script::Compile(scriptSource);
   if (script.IsEmpty())
   {
      //context.Dispose();
      v8::Handle<v8::Object>::Cast(mMainContext->Global()->GetPrototype())->SetPointerInInternalField(1, NULL);
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
   //context.Dispose();
   v8::Handle<v8::Object>::Cast(mMainContext->Global()->GetPrototype())->SetPointerInInternalField(1, NULL);
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

bool JSInterpreter::setArguments(PlugInArgList* pInArgList, PlugInArgList* pOutArgList)
{
   mpInArgList = pInArgList;
   mpOutArgList = pOutArgList;
   return true;
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
   // We'll set internal field to hold various Opticks pointers
   // Internal field 0 = the JSInterpreter this pointer
   // Internal field 1 = the current Progress pointer or NULL
   mGlobalTemplate->SetInternalFieldCount(2);

   v8::Local<v8::ObjectTemplate> system_template = v8::ObjectTemplate::New();
   system_template->Set("write_stdout", v8::FunctionTemplate::New(send_out_callback));
   system_template->Set("write_stderr", v8::FunctionTemplate::New(send_error_callback));
   mGlobalTemplate->Set(v8::String::NewSymbol("system"), system_template, v8::PropertyAttribute(v8::ReadOnly | v8::DontDelete));

   mGlobalTemplate->Set(v8::String::NewSymbol("require"), v8::FunctionTemplate::New(require), v8::PropertyAttribute(v8::ReadOnly | v8::DontEnum));

   mGlobalTemplate->Set(v8::String::NewSymbol("PlugIn"), v8::FunctionTemplate::New(create_plugin), v8::ReadOnly);
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
