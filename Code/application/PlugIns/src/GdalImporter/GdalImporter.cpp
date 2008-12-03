/*
 * The information in this file is
 * Copyright(c) 2007 Ball Aerospace & Technologies Corporation
 * and is subject to the terms and conditions of the
 * GNU Lesser General Public License Version 2.1
 * The license text is available from   
 * http://www.gnu.org/licenses/lgpl.html
 */

#include "AppVersion.h"
#include "DynamicObject.h"
#include "Endian.h"
#include "GdalImporter.h"
#include "GdalRasterPager.h"
#include "ImportDescriptor.h"
#include "PlugInArgList.h"
#include "PlugInResource.h"
#include "RasterDataDescriptor.h"
#include "RasterElement.h"
#include "RasterFileDescriptor.h"
#include "RasterUtilities.h"
#include "SpecialMetadata.h"
#include "StringUtilities.h"
#include "TypeConverter.h"

#include <gdal_priv.h>

namespace
{
   EncodingType gdalDataTypeToEncodingType(GDALDataType type)
   {
      switch(type)
      {
      case GDT_Byte:
         return INT1UBYTE;
      case GDT_UInt16:
         return INT2UBYTES;
      case GDT_Int16:
         return INT2SBYTES;
      case GDT_UInt32:
         return INT4UBYTES;
      case GDT_Int32:
         return INT4SBYTES;
      case GDT_Float32:
         return FLT4BYTES;
      case GDT_Float64:
         return FLT8BYTES;
      case GDT_CInt16:
         return INT4SCOMPLEX;
      case GDT_CInt32:
      case GDT_CFloat32:
      case GDT_CFloat64:
         return FLT8COMPLEX;
      default:
         break;
      }
      return EncodingType();
   }

   size_t gdalDataTypeSize(GDALDataType type)
   {
      switch(type)
      {
      case GDT_Byte:
         return 1;
      case GDT_UInt16:
      case GDT_Int16:
         return 2;
      case GDT_UInt32:
      case GDT_Int32:
      case GDT_Float32:
      case GDT_CInt16:
         return 4;
      case GDT_Float64:
      case GDT_CInt32:
      case GDT_CFloat32:
         return 8;
      case GDT_CFloat64:
         return 16;
      }
      return 0;
   }
}

GdalImporter::GdalImporter()
{
   setDescriptorId("{842c4da3-9d83-4301-8f56-b71210d1afd4}");
   setName("Generic GDAL Importer");
   setCreator("Ball Aerospace & Technologies Corp.");
   setCopyright(APP_COPYRIGHT);
   setVersion(APP_VERSION_NUMBER);
   setProductionStatus(APP_IS_PRODUCTION_RELEASE);
   addDependencyCopyright("GDAL", "<p>Copyright (c) 2000, Frank Warmerdam</p>"
      "<p>Permission is hereby granted, free of charge, to any person obtaining a"
"copy of this software and associated documentation files (the \"Software\"),"
"to deal in the Software without restriction, including without limitation"
"the rights to use, copy, modify, merge, publish, distribute, sublicense,"
"and/or sell copies of the Software, and to permit persons to whom the"
"Software is furnished to do so, subject to the following conditions:"
"<blockquote>The above copyright notice and this permission notice shall be included"
"in all copies or substantial portions of the Software.</blockquote></p>"
"<p>THE SOFTWARE IS PROVIDED \"AS IS\", WITHOUT WARRANTY OF ANY KIND, EXPRESS"
"OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,"
"FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL"
"THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER"
"LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING"
"FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER"
"DEALINGS IN THE SOFTWARE.</p>");

   GDALAllRegister();
   std::string desc = "Import files using the GDAL library. The following file types are supported:\n";
   GDALDriverManager* pManager = GetGDALDriverManager();
   if (pManager != NULL)
   {
      for (int driver = 0; driver < pManager->GetDriverCount(); driver++)
      {
         GDALDriver* pDriver = pManager->GetDriver(driver);
         if (pDriver != NULL)
         {
            if (driver > 0 && driver % 5 == 0)
            {
               desc += "\n";
            }
            else if (driver > 0)
            {
               desc += ", ";
            }
            desc += pDriver->GetDescription();
         }
      }
   }
   setDescription(desc);
}

GdalImporter::~GdalImporter()
{
}

std::vector<ImportDescriptor*> GdalImporter::getImportDescriptors(const std::string& filename)
{
   mErrors.clear();
   mWarnings.clear();
   std::vector<ImportDescriptor*> descriptors;

   std::auto_ptr<GDALDataset> pDataset(reinterpret_cast<GDALDataset*>(GDALOpen(filename.c_str(), GA_ReadOnly)));
   if (pDataset.get() == NULL)
   {
      mErrors.push_back("GDAL does not recognize the dataset");
      return descriptors;
   }
   ImportDescriptorResource pImportDescriptor(filename, TypeConverter::toString<RasterElement>());
   descriptors.push_back(pImportDescriptor.release());

   GDALDataType rawType = pDataset->GetRasterBand(1)->GetRasterDataType();
   EncodingType encoding = gdalDataTypeToEncodingType(rawType);
   if (!encoding.isValid())
   {
      mErrors.push_back(std::string("Unsupported data type ") + GDALGetDataTypeName(rawType));
      // continue so we get more errors and have an ImportDescriptor so this error is displayed to the user
   }
   else if (rawType == GDT_CFloat64)
   {
      mWarnings.push_back("64-bit Complex float not fully supported. Data will be loaded but may be truncated.");
   }
   pImportDescriptor->setDataDescriptor(RasterUtilities::generateRasterDataDescriptor(
      filename, NULL, pDataset->GetRasterYSize(), pDataset->GetRasterXSize(), pDataset->GetRasterCount(),
      BSQ, encoding, IN_MEMORY));
   RasterFileDescriptor* pFileDesc = 
      dynamic_cast<RasterFileDescriptor*>(RasterUtilities::generateAndSetFileDescriptor(
      pImportDescriptor->getDataDescriptor(), filename, "", Endian::getSystemEndian()));

   if (pFileDesc != NULL && pDataset->GetGCPCount() > 0)
   {
      std::list<GcpPoint> gcps;
      const GDAL_GCP *pGcps = pDataset->GetGCPs();
      for (int gcpnum = 0; gcpnum < pDataset->GetGCPCount(); gcpnum++)
      {
         GcpPoint gcp;
         gcp.mPixel.mX = pGcps[gcpnum].dfGCPPixel;
         gcp.mPixel.mY = pGcps[gcpnum].dfGCPLine;
         gcp.mCoordinate.mX = pGcps[gcpnum].dfGCPX;
         gcp.mCoordinate.mY = pGcps[gcpnum].dfGCPY;
         gcps.push_back(gcp);
      }
      pFileDesc->setGcps(gcps);
   }

   DynamicObject* pMetadata = pImportDescriptor->getDataDescriptor()->getMetadata();
   VERIFYRV(pMetadata, descriptors);
   char** pDatasetMetadata = pDataset->GetMetadata();
   if (pDatasetMetadata != NULL)
   {
      for (size_t idx = 0; pDatasetMetadata[idx] != NULL; idx++)
      {
         std::string entry(pDatasetMetadata[idx]);
         std::vector<std::string> kvpair = StringUtilities::split(entry, '=');
         if (kvpair.size() == 1)
         {
            kvpair.push_back("");
         }
         if (kvpair.front() == SPECIAL_METADATA_NAME)
         {
            // don't want to accidentally set the special metadata dynamic object to some other type
            pMetadata->setAttribute(std::string("GDAL ") + SPECIAL_METADATA_NAME, kvpair.back());
         }
         else if (kvpair.front() == "Projection")
         {
            // we explicitly add this below...don't overwrite it
            pMetadata->setAttribute("GDAL Projection", kvpair.back());
         }
         else
         {
            pMetadata->setAttribute(kvpair.front(), kvpair.back());
         }
      }
   }
   pMetadata->setAttribute("Projection", std::string(pDataset->GetProjectionRef()));

   return descriptors;
}

unsigned char GdalImporter::getFileAffinity(const std::string& filename)
{
   std::auto_ptr<GDALDataset> pDataset(reinterpret_cast<GDALDataset*>(GDALOpen(filename.c_str(), GA_ReadOnly)));
   if (pDataset.get() != NULL)
   {
      return CAN_LOAD_FILE_TYPE;
   }
   return CAN_NOT_LOAD;
}

bool GdalImporter::validate(const DataDescriptor* pDescriptor, std::string& errorMessage) const
{
   errorMessage = "";
   if (!mErrors.empty())
   {
      errorMessage = StringUtilities::join(mErrors, "\n");
      return false;
   }
   std::string baseErrorMessage;
   bool valid = RasterElementImporterShell::validate(pDescriptor, baseErrorMessage);
   if (!mWarnings.empty())
   {
      if (!baseErrorMessage.empty())
      {
         errorMessage += baseErrorMessage + "\n";
      }
      errorMessage += StringUtilities::join(mWarnings, "\n");
   }
   else
   {
      errorMessage = baseErrorMessage;
   }
   return valid;
}

bool GdalImporter::validateDefaultOnDiskReadOnly(const DataDescriptor* pDescriptor, std::string& errorMessage) const
{
   const RasterDataDescriptor* pRasterDescriptor = dynamic_cast<const RasterDataDescriptor*>(pDescriptor);
   if (pRasterDescriptor == NULL)
   {
      errorMessage = "The data descriptor is invalid!";
      return false;
   }

   const RasterFileDescriptor* pFileDescriptor =
      dynamic_cast<const RasterFileDescriptor*>(pRasterDescriptor->getFileDescriptor());
   if (pFileDescriptor == NULL)
   {
      errorMessage = "The file descriptor is invalid!";
      return false;
   }

   ProcessingLocation loc = pDescriptor->getProcessingLocation();
   if (loc == ON_DISK_READ_ONLY)
   {
      // Interleave conversions
      InterleaveFormatType dataInterleave = pRasterDescriptor->getInterleaveFormat();
      InterleaveFormatType fileInterleave = pFileDescriptor->getInterleaveFormat();
      if (pRasterDescriptor->getBandCount() > 1 && dataInterleave != fileInterleave)
      {
         errorMessage = "Interleave format conversions are not supported with on-disk read-only processing!";
         return false;
      }
   }

   return true;
}

bool GdalImporter::createRasterPager(RasterElement* pRaster) const
{
   VERIFY(pRaster != NULL);
   DataDescriptor *pDescriptor = pRaster->getDataDescriptor();
   VERIFY(pDescriptor != NULL);
   FileDescriptor *pFileDescriptor = pDescriptor->getFileDescriptor();
   VERIFY(pFileDescriptor != NULL);

   std::string filename = pRaster->getFilename();
   Progress *pProgress = getProgress();

   FactoryResource<Filename> pFilename;
   pFilename->setFullPathAndName(filename);

   ExecutableResource pagerPlugIn("GDAL Raster Pager", std::string(), pProgress);
   pagerPlugIn->getInArgList().setPlugInArgValue(CachedPager::PagedElementArg(), pRaster);
   pagerPlugIn->getInArgList().setPlugInArgValue(CachedPager::PagedFilenameArg(), pFilename.get());

   bool success = pagerPlugIn->execute();

   RasterPager *pPager = dynamic_cast<RasterPager*>(pagerPlugIn->getPlugIn());
   if (!success || pPager == NULL)
   {
      std::string message = "Execution of GDAL Raster Pager failed!";
      if (pProgress != NULL) pProgress->updateProgress(message, 0, ERRORS);
      return false;
   }

   pRaster->setPager(pPager);
   pagerPlugIn->releasePlugIn();

   return true;
}