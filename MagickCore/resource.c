/*
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%                                                                             %
%                                                                             %
%                                                                             %
%           RRRR    EEEEE   SSSSS   OOO   U   U  RRRR    CCCC  EEEEE          %
%           R   R   E       SS     O   O  U   U  R   R  C      E              %
%           RRRR    EEE      SSS   O   O  U   U  RRRR   C      EEE            %
%           R R     E          SS  O   O  U   U  R R    C      E              %
%           R  R    EEEEE   SSSSS   OOO    UUU   R  R    CCCC  EEEEE          %
%                                                                             %
%                                                                             %
%                        Get/Set MagickCore Resources                         %
%                                                                             %
%                              Software Design                                %
%                                   Cristy                                    %
%                               September 2002                                %
%                                                                             %
%                                                                             %
%  Copyright 1999-2017 ImageMagick Studio LLC, a non-profit organization      %
%  dedicated to making software imaging solutions freely available.           %
%                                                                             %
%  You may not use this file except in compliance with the License.  You may  %
%  obtain a copy of the License at                                            %
%                                                                             %
%    http://www.imagemagick.org/script/license.php                            %
%                                                                             %
%  Unless required by applicable law or agreed to in writing, software        %
%  distributed under the License is distributed on an "AS IS" BASIS,          %
%  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.   %
%  See the License for the specific language governing permissions and        %
%  limitations under the License.                                             %
%                                                                             %
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%
%
*/

/*
  Include declarations.
*/
#include "MagickCore/studio.h"
#include "MagickCore/cache.h"
#include "MagickCore/cache-private.h"
#include "MagickCore/configure.h"
#include "MagickCore/exception.h"
#include "MagickCore/exception-private.h"
#include "MagickCore/linked-list.h"
#include "MagickCore/log.h"
#include "MagickCore/image.h"
#include "MagickCore/image-private.h"
#include "MagickCore/memory_.h"
#include "MagickCore/nt-base-private.h"
#include "MagickCore/option.h"
#include "MagickCore/policy.h"
#include "MagickCore/random_.h"
#include "MagickCore/registry.h"
#include "MagickCore/resource_.h"
#include "MagickCore/resource-private.h"
#include "MagickCore/semaphore.h"
#include "MagickCore/signature-private.h"
#include "MagickCore/string_.h"
#include "MagickCore/string-private.h"
#include "MagickCore/splay-tree.h"
#include "MagickCore/thread-private.h"
#include "MagickCore/token.h"
#include "MagickCore/utility.h"
#include "MagickCore/utility-private.h"

/*
  Define declarations.
*/
#define MagickPathTemplate "XXXXXXXXXXXX"

/*
  Typedef declarations.
*/
typedef struct _ResourceInfo
{
  MagickOffsetType
    width,
    height,
    area,
    memory,
    map,
    disk,
    file,
    thread,
    throttle,
    time;

  MagickSizeType
    width_limit,
    height_limit,
    area_limit,
    memory_limit,
    map_limit,
    disk_limit,
    file_limit,
    thread_limit,
    throttle_limit,
    time_limit;
} ResourceInfo;

/*
  Global declarations.
*/
static RandomInfo
  *random_info = (RandomInfo *) NULL;

static ResourceInfo
  resource_info =
  {
    MagickULLConstant(0),              /* initial width */
    MagickULLConstant(0),              /* initial height */
    MagickULLConstant(0),              /* initial area */
    MagickULLConstant(0),              /* initial memory */
    MagickULLConstant(0),              /* initial map */
    MagickULLConstant(0),              /* initial disk */
    MagickULLConstant(0),              /* initial file */
    MagickULLConstant(0),              /* initial thread */
    MagickULLConstant(0),              /* initial throttle */
    MagickULLConstant(0),              /* initial time */
    (INT_MAX/(5*sizeof(Quantum))),     /* width limit */
    (INT_MAX/(5*sizeof(Quantum))),     /* height limit */
    MagickULLConstant(3072)*1024*1024, /* area limit */
    MagickULLConstant(1536)*1024*1024, /* memory limit */
    MagickULLConstant(3072)*1024*1024, /* map limit */
    MagickResourceInfinity,            /* disk limit */
    MagickULLConstant(768),            /* file limit */
    MagickULLConstant(1),              /* thread limit */
    MagickULLConstant(0),              /* throttle limit */
    MagickResourceInfinity             /* time limit */
  };

static SemaphoreInfo
  *resource_semaphore = (SemaphoreInfo *) NULL;

static SplayTreeInfo
  *temporary_resources = (SplayTreeInfo *) NULL;

/*
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%                                                                             %
%                                                                             %
%                                                                             %
%   A c q u i r e M a g i c k R e s o u r c e                                 %
%                                                                             %
%                                                                             %
%                                                                             %
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%
%  AcquireMagickResource() acquires resources of the specified type.
%  MagickFalse is returned if the specified resource is exhausted otherwise
%  MagickTrue.
%
%  The format of the AcquireMagickResource() method is:
%
%      MagickBooleanType AcquireMagickResource(const ResourceType type,
%        const MagickSizeType size)
%
%  A description of each parameter follows:
%
%    o type: the type of resource.
%
%    o size: the number of bytes needed from for this resource.
%
*/
MagickExport MagickBooleanType AcquireMagickResource(const ResourceType type,
  const MagickSizeType size)
{
  char
    resource_current[MagickFormatExtent],
    resource_limit[MagickFormatExtent],
    resource_request[MagickFormatExtent];

  MagickBooleanType
    status;

  MagickSizeType
    limit;

  status=MagickFalse;
  (void) FormatMagickSize(size,MagickFalse,"B",MagickFormatExtent,
    resource_request);
  if (resource_semaphore == (SemaphoreInfo *) NULL)
    ActivateSemaphoreInfo(&resource_semaphore);
  LockSemaphoreInfo(resource_semaphore);
  switch (type)
  {
    case AreaResource:
    {
      resource_info.area=(MagickOffsetType) size;
      limit=resource_info.area_limit;
      status=(resource_info.area_limit == MagickResourceInfinity) ||
        (size < limit) ? MagickTrue : MagickFalse;
      (void) FormatMagickSize((MagickSizeType) resource_info.area,MagickFalse,
        "P",MagickFormatExtent,resource_current);
      (void) FormatMagickSize(resource_info.area_limit,MagickFalse,"P",
        MagickFormatExtent,resource_limit);
      break;
    }
    case MemoryResource:
    {
      resource_info.memory+=size;
      limit=resource_info.memory_limit;
      status=(resource_info.memory_limit == MagickResourceInfinity) ||
        ((MagickSizeType) resource_info.memory < limit) ? MagickTrue :
        MagickFalse;
      (void) FormatMagickSize((MagickSizeType) resource_info.memory,MagickTrue,
        "B",MagickFormatExtent,resource_current);
      (void) FormatMagickSize(resource_info.memory_limit,MagickTrue,"B",
        MagickFormatExtent,resource_limit);
      break;
    }
    case MapResource:
    {
      resource_info.map+=size;
      limit=resource_info.map_limit;
      status=(resource_info.map_limit == MagickResourceInfinity) ||
        ((MagickSizeType) resource_info.map < limit) ? MagickTrue : MagickFalse;
      (void) FormatMagickSize((MagickSizeType) resource_info.map,MagickTrue,
        "B",MagickFormatExtent,resource_current);
      (void) FormatMagickSize(resource_info.map_limit,MagickTrue,"B",
        MagickFormatExtent,resource_limit);
      break;
    }
    case DiskResource:
    {
      resource_info.disk+=size;
      limit=resource_info.disk_limit;
      status=(resource_info.disk_limit == MagickResourceInfinity) ||
        ((MagickSizeType) resource_info.disk < limit) ? MagickTrue :
        MagickFalse;
      (void) FormatMagickSize((MagickSizeType) resource_info.disk,MagickTrue,
        "B",MagickFormatExtent,resource_current);
      (void) FormatMagickSize(resource_info.disk_limit,MagickTrue,"B",
        MagickFormatExtent,resource_limit);
      break;
    }
    case FileResource:
    {
      resource_info.file+=size;
      limit=resource_info.file_limit;
      status=(resource_info.file_limit == MagickResourceInfinity) ||
        ((MagickSizeType) resource_info.file < limit) ?
        MagickTrue : MagickFalse;
      (void) FormatMagickSize((MagickSizeType) resource_info.file,MagickFalse,
        "B",MagickFormatExtent,resource_current);
      (void) FormatMagickSize((MagickSizeType) resource_info.file_limit,
        MagickFalse,"B",MagickFormatExtent,resource_limit);
      break;
    }
    case HeightResource:
    {
      resource_info.area=(MagickOffsetType) size;
      limit=resource_info.height_limit;
      status=(resource_info.area_limit == MagickResourceInfinity) ||
        (size < limit) ? MagickTrue : MagickFalse;
      (void) FormatMagickSize((MagickSizeType) resource_info.height,MagickFalse,
        "P",MagickFormatExtent,resource_current);
      (void) FormatMagickSize(resource_info.height_limit,MagickFalse,"P",
        MagickFormatExtent,resource_limit);
      break;
    }
    case ThreadResource:
    {
      limit=resource_info.thread_limit;
      status=(resource_info.thread_limit == MagickResourceInfinity) ||
        ((MagickSizeType) resource_info.thread < limit) ?
        MagickTrue : MagickFalse;
      (void) FormatMagickSize((MagickSizeType) resource_info.thread,MagickFalse,
        "B",MagickFormatExtent,resource_current);
      (void) FormatMagickSize((MagickSizeType) resource_info.thread_limit,
        MagickFalse,"B",MagickFormatExtent,resource_limit);
      break;
    }
    case ThrottleResource:
    {
      limit=resource_info.throttle_limit;
      status=(resource_info.throttle_limit == MagickResourceInfinity) ||
        ((MagickSizeType) resource_info.throttle < limit) ?
        MagickTrue : MagickFalse;
      (void) FormatMagickSize((MagickSizeType) resource_info.throttle,
        MagickFalse,"B",MagickFormatExtent,resource_current);
      (void) FormatMagickSize((MagickSizeType) resource_info.throttle_limit,
        MagickFalse,"B",MagickFormatExtent,resource_limit);
      break;
    }
    case TimeResource:
    {
      resource_info.time+=size;
      limit=resource_info.time_limit;
      status=(resource_info.time_limit == MagickResourceInfinity) ||
        ((MagickSizeType) resource_info.time < limit) ?
        MagickTrue : MagickFalse;
      (void) FormatMagickSize((MagickSizeType) resource_info.time,MagickFalse,
        "B",MagickFormatExtent,resource_current);
      (void) FormatMagickSize((MagickSizeType) resource_info.time_limit,
        MagickFalse,"B",MagickFormatExtent,resource_limit);
      break;
    }
    case WidthResource:
    {
      resource_info.area=(MagickOffsetType) size;
      limit=resource_info.width_limit;
      status=(resource_info.area_limit == MagickResourceInfinity) ||
        (size < limit) ? MagickTrue : MagickFalse;
      (void) FormatMagickSize((MagickSizeType) resource_info.width,MagickFalse,
        "P",MagickFormatExtent,resource_current);
      (void) FormatMagickSize(resource_info.width_limit,MagickFalse,"P",
        MagickFormatExtent,resource_limit);
      break;
    }
    default:
      break;
  }
  UnlockSemaphoreInfo(resource_semaphore);
  (void) LogMagickEvent(ResourceEvent,GetMagickModule(),"%s: %s/%s/%s",
    CommandOptionToMnemonic(MagickResourceOptions,(ssize_t) type),
    resource_request,resource_current,resource_limit);
  return(status);
}

/*
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%                                                                             %
%                                                                             %
%                                                                             %
+   A s y n c h r o n o u s R e s o u r c e C o m p o n e n t T e r m i n u s %
%                                                                             %
%                                                                             %
%                                                                             %
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%
%  AsynchronousResourceComponentTerminus() destroys the resource environment.
%  It differs from ResourceComponentTerminus() in that it can be called from a
%  asynchronous signal handler.
%
%  The format of the ResourceComponentTerminus() method is:
%
%      ResourceComponentTerminus(void)
%
*/
MagickPrivate void AsynchronousResourceComponentTerminus(void)
{
  const char
    *path;

  if (temporary_resources == (SplayTreeInfo *) NULL)
    return;
  /*
    Remove any lingering temporary files.
  */
  ResetSplayTreeIterator(temporary_resources);
  path=(const char *) GetNextKeyInSplayTree(temporary_resources);
  while (path != (const char *) NULL)
  {
    (void) ShredFile(path);
    path=(const char *) GetNextKeyInSplayTree(temporary_resources);
  }
}

/*
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%                                                                             %
%                                                                             %
%                                                                             %
%   A c q u i r e U n i q u e F i l e R e s o u r c e                         %
%                                                                             %
%                                                                             %
%                                                                             %
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%
%  AcquireUniqueFileResource() returns a unique file name, and returns a file
%  descriptor for the file open for reading and writing.
%
%  The format of the AcquireUniqueFileResource() method is:
%
%      int AcquireUniqueFileResource(char *path)
%
%  A description of each parameter follows:
%
%   o  path:  Specifies a pointer to an array of characters.  The unique path
%      name is returned in this array.
%
*/

static void *DestroyTemporaryResources(void *temporary_resource)
{
  (void) ShredFile((char *) temporary_resource);
  temporary_resource=DestroyString((char *) temporary_resource);
  return((void *) NULL);
}

MagickExport MagickBooleanType GetPathTemplate(char *path)
{
  char
    *directory,
    *value;

  ExceptionInfo
    *exception;

  MagickBooleanType
    status;

  struct stat
    attributes;

  (void) FormatLocaleString(path,MagickPathExtent,"magick-%.20g"
    MagickPathTemplate,(double) getpid());
  exception=AcquireExceptionInfo();
  directory=(char *) GetImageRegistry(StringRegistryType,"temporary-path",
    exception);
  exception=DestroyExceptionInfo(exception);
  if (directory == (char *) NULL)
    directory=GetEnvironmentValue("MAGICK_TEMPORARY_PATH");
  if (directory == (char *) NULL)
    directory=GetEnvironmentValue("MAGICK_TMPDIR");
  if (directory == (char *) NULL)
    directory=GetEnvironmentValue("TMPDIR");
#if defined(MAGICKCORE_WINDOWS_SUPPORT) || defined(__OS2__) || defined(__CYGWIN__)
  if (directory == (char *) NULL)
    directory=GetEnvironmentValue("TMP");
  if (directory == (char *) NULL)
    directory=GetEnvironmentValue("TEMP");
#endif
#if defined(__VMS)
  if (directory == (char *) NULL)
    directory=GetEnvironmentValue("MTMPDIR");
#endif
#if defined(P_tmpdir)
  if (directory == (char *) NULL)
    directory=ConstantString(P_tmpdir);
#endif
  if (directory == (char *) NULL)
    return(MagickTrue);
  value=GetPolicyValue("resource:temporary-path");
  if (value != (char *) NULL)
    {
      (void) CloneString(&directory,value);
      value=DestroyString(value);
    }
  if (strlen(directory) > (MagickPathExtent-25))
    {
      directory=DestroyString(directory);
      return(MagickFalse);
    }
  status=GetPathAttributes(directory,&attributes);
  if ((status == MagickFalse) || !S_ISDIR(attributes.st_mode))
    {
      directory=DestroyString(directory);
      return(MagickFalse);
    }
  if (directory[strlen(directory)-1] == *DirectorySeparator)
    (void) FormatLocaleString(path,MagickPathExtent,
      "%smagick-%.20g" MagickPathTemplate,directory,(double) getpid());
  else
    (void) FormatLocaleString(path,MagickPathExtent,
      "%s%smagick-%.20g" MagickPathTemplate,directory,DirectorySeparator,
      (double) getpid());
  directory=DestroyString(directory);
#if defined(MAGICKCORE_WINDOWS_SUPPORT)
  {
    register char
      *p;

    /*
      Ghostscript does not like backslashes so we need to replace them. The
      forward slash also works under Windows.
    */
    for (p=(path[1] == *DirectorySeparator ? path+2 : path); *p != '\0'; p++)
      if (*p == *DirectorySeparator)
        *p='/';
  }
#endif
  return(MagickTrue);
}

MagickExport int AcquireUniqueFileResource(char *path)
{
#if !defined(O_NOFOLLOW)
#define O_NOFOLLOW 0
#endif
#if !defined(TMP_MAX)
# define TMP_MAX  238328
#endif

  int
    c,
    file;

  register char
    *p;

  register ssize_t
    i;

  static const char
    portable_filename[65] =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_-";

  StringInfo
    *key;

  unsigned char
    *datum;

  assert(path != (char *) NULL);
  (void) LogMagickEvent(ResourceEvent,GetMagickModule(),"...");
  if (random_info == (RandomInfo *) NULL)
    {
      LockSemaphoreInfo(resource_semaphore);
      if (random_info == (RandomInfo *) NULL)
        random_info=AcquireRandomInfo();
      UnlockSemaphoreInfo(resource_semaphore);
    }
  file=(-1);
  for (i=0; i < (ssize_t) TMP_MAX; i++)
  {
    /*
      Get temporary pathname.
    */
    (void) GetPathTemplate(path);
    key=GetRandomKey(random_info,6);
    p=path+strlen(path)-strlen(MagickPathTemplate);
    datum=GetStringInfoDatum(key);
    for (i=0; i < (ssize_t) GetStringInfoLength(key); i++)
    {
      c=(int) (datum[i] & 0x3f);
      *p++=portable_filename[c];
    }
    key=DestroyStringInfo(key);
#if defined(MAGICKCORE_HAVE_MKSTEMP)
    file=mkstemp(path);
    if (file != -1)
      {
#if defined(MAGICKCORE_HAVE_FCHMOD)
        (void) fchmod(file,0600);
#endif
#if defined(__OS2__)
        setmode(file,O_BINARY);
#endif
        break;
      }
#endif
    key=GetRandomKey(random_info,strlen(MagickPathTemplate));
    p=path+strlen(path)-strlen(MagickPathTemplate);
    datum=GetStringInfoDatum(key);
    for (i=0; i < (ssize_t) GetStringInfoLength(key); i++)
    {
      c=(int) (datum[i] & 0x3f);
      *p++=portable_filename[c];
    }
    key=DestroyStringInfo(key);
    file=open_utf8(path,O_RDWR | O_CREAT | O_EXCL | O_BINARY | O_NOFOLLOW,
      S_MODE);
    if ((file >= 0) || (errno != EEXIST))
      break;
  }
  (void) LogMagickEvent(ResourceEvent,GetMagickModule(),"%s",path);
  if (file == -1)
    return(file);
  if (resource_semaphore == (SemaphoreInfo *) NULL)
    ActivateSemaphoreInfo(&resource_semaphore);
  LockSemaphoreInfo(resource_semaphore);
  if (temporary_resources == (SplayTreeInfo *) NULL)
    temporary_resources=NewSplayTree(CompareSplayTreeString,
      DestroyTemporaryResources,(void *(*)(void *)) NULL);
  UnlockSemaphoreInfo(resource_semaphore);
  (void) AddValueToSplayTree(temporary_resources,ConstantString(path),
    (const void *) NULL);
  return(file);
}

/*
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%                                                                             %
%                                                                             %
%                                                                             %
%   G e t M a g i c k R e s o u r c e                                         %
%                                                                             %
%                                                                             %
%                                                                             %
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%
%  GetMagickResource() returns the specified resource.
%
%  The format of the GetMagickResource() method is:
%
%      MagickSizeType GetMagickResource(const ResourceType type)
%
%  A description of each parameter follows:
%
%    o type: the type of resource.
%
*/
MagickExport MagickSizeType GetMagickResource(const ResourceType type)
{
  MagickSizeType
    resource;

  resource=0;
  LockSemaphoreInfo(resource_semaphore);
  switch (type)
  {
    case WidthResource:
    {
      resource=(MagickSizeType) resource_info.width;
      break;
    }
    case HeightResource:
    {
      resource=(MagickSizeType) resource_info.height;
      break;
    }
    case AreaResource:
    {
      resource=(MagickSizeType) resource_info.area;
      break;
    }
    case MemoryResource:
    {
      resource=(MagickSizeType) resource_info.memory;
      break;
    }
    case MapResource:
    {
      resource=(MagickSizeType) resource_info.map;
      break;
    }
    case DiskResource:
    {
      resource=(MagickSizeType) resource_info.disk;
      break;
    }
    case FileResource:
    {
      resource=(MagickSizeType) resource_info.file;
      break;
    }
    case ThreadResource:
    {
      resource=(MagickSizeType) resource_info.thread;
      break;
    }
    case ThrottleResource:
    {
      resource=(MagickSizeType) resource_info.throttle;
      break;
    }
    case TimeResource:
    {
      resource=(MagickSizeType) resource_info.time;
      break;
    }
    default:
      break;
  }
  UnlockSemaphoreInfo(resource_semaphore);
  return(resource);
}

/*
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%                                                                             %
%                                                                             %
%                                                                             %
%   G e t M a g i c k R e s o u r c e L i m i t                               %
%                                                                             %
%                                                                             %
%                                                                             %
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%
%  GetMagickResourceLimit() returns the specified resource limit.
%
%  The format of the GetMagickResourceLimit() method is:
%
%      MagickSizeType GetMagickResourceLimit(const ResourceType type)
%
%  A description of each parameter follows:
%
%    o type: the type of resource.
%
*/
MagickExport MagickSizeType GetMagickResourceLimit(const ResourceType type)
{
  MagickSizeType
    resource;

  resource=0;
  if (resource_semaphore == (SemaphoreInfo *) NULL)
    ActivateSemaphoreInfo(&resource_semaphore);
  LockSemaphoreInfo(resource_semaphore);
  switch (type)
  {
    case WidthResource:
    {
      resource=resource_info.width_limit;
      break;
    }
    case HeightResource:
    {
      resource=resource_info.height_limit;
      break;
    }
    case AreaResource:
    {
      resource=resource_info.area_limit;
      break;
    }
    case MemoryResource:
    {
      resource=resource_info.memory_limit;
      break;
    }
    case MapResource:
    {
      resource=resource_info.map_limit;
      break;
    }
    case DiskResource:
    {
      resource=resource_info.disk_limit;
      break;
    }
    case FileResource:
    {
      resource=resource_info.file_limit;
      break;
    }
    case ThreadResource:
    {
      resource=resource_info.thread_limit;
      break;
    }
    case ThrottleResource:
    {
      resource=resource_info.throttle_limit;
      break;
    }
    case TimeResource:
    {
      resource=resource_info.time_limit;
      break;
    }
    default:
      break;
  }
  UnlockSemaphoreInfo(resource_semaphore);
  return(resource);
}

/*
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%                                                                             %
%                                                                             %
%                                                                             %
%  L i s t M a g i c k R e s o u r c e I n f o                                %
%                                                                             %
%                                                                             %
%                                                                             %
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%
%  ListMagickResourceInfo() lists the resource info to a file.
%
%  The format of the ListMagickResourceInfo method is:
%
%      MagickBooleanType ListMagickResourceInfo(FILE *file,
%        ExceptionInfo *exception)
%
%  A description of each parameter follows.
%
%    o file:  An pointer to a FILE.
%
%    o exception: return any errors or warnings in this structure.
%
*/
MagickExport MagickBooleanType ListMagickResourceInfo(FILE *file,
  ExceptionInfo *magick_unused(exception))
{
  char
    area_limit[MagickFormatExtent],
    disk_limit[MagickFormatExtent],
    height_limit[MagickFormatExtent],
    map_limit[MagickFormatExtent],
    memory_limit[MagickFormatExtent],
    time_limit[MagickFormatExtent],
    width_limit[MagickFormatExtent];

  magick_unreferenced(exception);

  if (file == (const FILE *) NULL)
    file=stdout;
  if (resource_semaphore == (SemaphoreInfo *) NULL)
    ActivateSemaphoreInfo(&resource_semaphore);
  LockSemaphoreInfo(resource_semaphore);
  (void) FormatMagickSize(resource_info.width_limit,MagickFalse,"P",
    MagickFormatExtent,width_limit);
  (void) FormatMagickSize(resource_info.height_limit,MagickFalse,"P",
    MagickFormatExtent,height_limit);
  (void) FormatMagickSize(resource_info.area_limit,MagickFalse,"P",
    MagickFormatExtent,area_limit);
  (void) FormatMagickSize(resource_info.memory_limit,MagickTrue,"B",
    MagickFormatExtent,memory_limit);
  (void) FormatMagickSize(resource_info.map_limit,MagickTrue,"B",
    MagickFormatExtent,map_limit);
  (void) CopyMagickString(disk_limit,"unlimited",MagickFormatExtent);
  if (resource_info.disk_limit != MagickResourceInfinity)
    (void) FormatMagickSize(resource_info.disk_limit,MagickTrue,"B",
      MagickFormatExtent,disk_limit);
  (void) CopyMagickString(time_limit,"unlimited",MagickFormatExtent);
  if (resource_info.time_limit != MagickResourceInfinity)
    (void) FormatLocaleString(time_limit,MagickFormatExtent,"%.20g",(double)
      ((MagickOffsetType) resource_info.time_limit));
  (void) FormatLocaleFile(file,"Resource limits:\n");
  (void) FormatLocaleFile(file,"  Width: %s\n",width_limit);
  (void) FormatLocaleFile(file,"  Height: %s\n",height_limit);
  (void) FormatLocaleFile(file,"  Area: %s\n",area_limit);
  (void) FormatLocaleFile(file,"  Memory: %s\n",memory_limit);
  (void) FormatLocaleFile(file,"  Map: %s\n",map_limit);
  (void) FormatLocaleFile(file,"  Disk: %s\n",disk_limit);
  (void) FormatLocaleFile(file,"  File: %.20g\n",(double) ((MagickOffsetType)
    resource_info.file_limit));
  (void) FormatLocaleFile(file,"  Thread: %.20g\n",(double) ((MagickOffsetType)
    resource_info.thread_limit));
  (void) FormatLocaleFile(file,"  Throttle: %.20g\n",(double)
    ((MagickOffsetType) resource_info.throttle_limit));
  (void) FormatLocaleFile(file,"  Time: %s\n",time_limit);
  (void) fflush(file);
  UnlockSemaphoreInfo(resource_semaphore);
  return(MagickTrue);
}

/*
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%                                                                             %
%                                                                             %
%                                                                             %
%   R e l i n q u i s h M a g i c k R e s o u r c e                           %
%                                                                             %
%                                                                             %
%                                                                             %
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%
%  RelinquishMagickResource() relinquishes resources of the specified type.
%
%  The format of the RelinquishMagickResource() method is:
%
%      void RelinquishMagickResource(const ResourceType type,
%        const MagickSizeType size)
%
%  A description of each parameter follows:
%
%    o type: the type of resource.
%
%    o size: the size of the resource.
%
*/
MagickExport void RelinquishMagickResource(const ResourceType type,
  const MagickSizeType size)
{
  char
    resource_current[MagickFormatExtent],
    resource_limit[MagickFormatExtent],
    resource_request[MagickFormatExtent];

  (void) FormatMagickSize(size,MagickFalse,"B",MagickFormatExtent,
    resource_request);
  if (resource_semaphore == (SemaphoreInfo *) NULL)
		ActivateSemaphoreInfo(&resource_semaphore);
  LockSemaphoreInfo(resource_semaphore);
  switch (type)
  {
    case WidthResource:
    {
      resource_info.width=(MagickOffsetType) size;
      (void) FormatMagickSize((MagickSizeType) resource_info.width,MagickFalse,
        "P",MagickFormatExtent,resource_current);
      (void) FormatMagickSize(resource_info.width_limit,MagickFalse,"P",
        MagickFormatExtent,resource_limit);
      break;
    }
    case HeightResource:
    {
      resource_info.height=(MagickOffsetType) size;
      (void) FormatMagickSize((MagickSizeType) resource_info.height,MagickFalse,
        "P",MagickFormatExtent,resource_current);
      (void) FormatMagickSize(resource_info.height_limit,MagickFalse,"P",
        MagickFormatExtent,resource_limit);
      break;
    }
    case AreaResource:
    {
      resource_info.area=(MagickOffsetType) size;
      (void) FormatMagickSize((MagickSizeType) resource_info.area,MagickFalse,
        "B",MagickFormatExtent,resource_current);
      (void) FormatMagickSize(resource_info.area_limit,MagickFalse,"B",
        MagickFormatExtent,resource_limit);
      break;
    }
    case MemoryResource:
    {
      resource_info.memory-=size;
      (void) FormatMagickSize((MagickSizeType) resource_info.memory,
        MagickTrue,"B",MagickFormatExtent,resource_current);
      (void) FormatMagickSize(resource_info.memory_limit,MagickTrue,"B",
        MagickFormatExtent,resource_limit);
      break;
    }
    case MapResource:
    {
      resource_info.map-=size;
      (void) FormatMagickSize((MagickSizeType) resource_info.map,MagickTrue,
        "B",MagickFormatExtent,resource_current);
      (void) FormatMagickSize(resource_info.map_limit,MagickTrue,"B",
        MagickFormatExtent,resource_limit);
      break;
    }
    case DiskResource:
    {
      resource_info.disk-=size;
      (void) FormatMagickSize((MagickSizeType) resource_info.disk,MagickTrue,
        "B",MagickFormatExtent,resource_current);
      (void) FormatMagickSize(resource_info.disk_limit,MagickTrue,"B",
        MagickFormatExtent,resource_limit);
      break;
    }
    case FileResource:
    {
      resource_info.file-=size;
      (void) FormatMagickSize((MagickSizeType) resource_info.file,MagickFalse,
        "B",MagickFormatExtent,resource_current);
      (void) FormatMagickSize((MagickSizeType) resource_info.file_limit,
        MagickFalse,"B",MagickFormatExtent,resource_limit);
      break;
    }
    case ThreadResource:
    {
      (void) FormatMagickSize((MagickSizeType) resource_info.thread,MagickFalse,
        "B",MagickFormatExtent,resource_current);
      (void) FormatMagickSize((MagickSizeType) resource_info.thread_limit,
        MagickFalse,"B",MagickFormatExtent,resource_limit);
      break;
    }
    case ThrottleResource:
    {
      (void) FormatMagickSize((MagickSizeType) resource_info.throttle,
        MagickFalse,"B",MagickFormatExtent,resource_current);
      (void) FormatMagickSize((MagickSizeType) resource_info.throttle_limit,
        MagickFalse,"B",MagickFormatExtent,resource_limit);
      break;
    }
    case TimeResource:
    {
      resource_info.time-=size;
      (void) FormatMagickSize((MagickSizeType) resource_info.time,MagickFalse,
        "B",MagickFormatExtent,resource_current);
      (void) FormatMagickSize((MagickSizeType) resource_info.time_limit,
        MagickFalse,"B",MagickFormatExtent,resource_limit);
      break;
    }
    default:
      break;
  }
  UnlockSemaphoreInfo(resource_semaphore);
  (void) LogMagickEvent(ResourceEvent,GetMagickModule(),"%s: %s/%s/%s",
    CommandOptionToMnemonic(MagickResourceOptions,(ssize_t) type),
      resource_request,resource_current,resource_limit);
}

/*
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%                                                                             %
%                                                                             %
%                                                                             %
%    R e l i n q u i s h U n i q u e F i l e R e s o u r c e                  %
%                                                                             %
%                                                                             %
%                                                                             %
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%
%  RelinquishUniqueFileResource() relinquishes a unique file resource.
%
%  The format of the RelinquishUniqueFileResource() method is:
%
%      MagickBooleanType RelinquishUniqueFileResource(const char *path)
%
%  A description of each parameter follows:
%
%    o name: the name of the temporary resource.
%
*/
MagickExport MagickBooleanType RelinquishUniqueFileResource(const char *path)
{
  char
    cache_path[MagickPathExtent];

  MagickBooleanType
    status;

  assert(path != (const char *) NULL);
  status=MagickFalse;
  (void) LogMagickEvent(ResourceEvent,GetMagickModule(),"%s",path);
  if (resource_semaphore == (SemaphoreInfo *) NULL)
    ActivateSemaphoreInfo(&resource_semaphore);
  LockSemaphoreInfo(resource_semaphore);
  if (temporary_resources != (SplayTreeInfo *) NULL)
    status=DeleteNodeFromSplayTree(temporary_resources, (const void *) path);
  UnlockSemaphoreInfo(resource_semaphore);
  (void) CopyMagickString(cache_path,path,MagickPathExtent);
  AppendImageFormat("cache",cache_path);
  (void) ShredFile(cache_path);
  if (status == MagickFalse)
    status=ShredFile(path);
  return(status);
}

/*
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%                                                                             %
%                                                                             %
%                                                                             %
+   R e s o u r c e C o m p o n e n t G e n e s i s                           %
%                                                                             %
%                                                                             %
%                                                                             %
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%
%  ResourceComponentGenesis() instantiates the resource component.
%
%  The format of the ResourceComponentGenesis method is:
%
%      MagickBooleanType ResourceComponentGenesis(void)
%
*/

static inline MagickSizeType StringToSizeType(const char *string,
  const double interval)
{
  double
    value;

  value=SiPrefixToDoubleInterval(string,interval);
  if (value >= (double) MagickULLConstant(~0))
    return(MagickULLConstant(~0));
  return((MagickSizeType) value);
}

MagickPrivate MagickBooleanType ResourceComponentGenesis(void)
{
  char
    *limit;

  MagickSizeType
    memory;

  ssize_t
    files,
    pages,
    pagesize;

  /*
    Set Magick resource limits.
  */
  if (resource_semaphore == (SemaphoreInfo *) NULL)
    resource_semaphore=AcquireSemaphoreInfo();
  (void) SetMagickResourceLimit(WidthResource,resource_info.width_limit);
  limit=GetEnvironmentValue("MAGICK_WIDTH_LIMIT");
  if (limit != (char *) NULL)
    {
      (void) SetMagickResourceLimit(WidthResource,StringToSizeType(limit,
        100.0));
      limit=DestroyString(limit);
    }
  (void) SetMagickResourceLimit(HeightResource,resource_info.height_limit);
  limit=GetEnvironmentValue("MAGICK_HEIGHT_LIMIT");
  if (limit != (char *) NULL)
    {
      (void) SetMagickResourceLimit(HeightResource,StringToSizeType(limit,
        100.0));
      limit=DestroyString(limit);
    }
  pagesize=GetMagickPageSize();
  pages=(-1);
#if defined(MAGICKCORE_HAVE_SYSCONF) && defined(_SC_PHYS_PAGES)
  pages=(ssize_t) sysconf(_SC_PHYS_PAGES);
#endif
  memory=(MagickSizeType) pages*pagesize;
  if ((pagesize <= 0) || (pages <= 0))
    memory=2048UL*1024UL*1024UL;
#if defined(PixelCacheThreshold)
  memory=PixelCacheThreshold;
#endif
  (void) SetMagickResourceLimit(AreaResource,2*memory);
  limit=GetEnvironmentValue("MAGICK_AREA_LIMIT");
  if (limit != (char *) NULL)
    {
      (void) SetMagickResourceLimit(AreaResource,StringToSizeType(limit,100.0));
      limit=DestroyString(limit);
    }
  (void) SetMagickResourceLimit(MemoryResource,memory);
  limit=GetEnvironmentValue("MAGICK_MEMORY_LIMIT");
  if (limit != (char *) NULL)
    {
      (void) SetMagickResourceLimit(MemoryResource,
        StringToSizeType(limit,100.0));
      limit=DestroyString(limit);
    }
  (void) SetMagickResourceLimit(MapResource,2*memory);
  limit=GetEnvironmentValue("MAGICK_MAP_LIMIT");
  if (limit != (char *) NULL)
    {
      (void) SetMagickResourceLimit(MapResource,StringToSizeType(limit,100.0));
      limit=DestroyString(limit);
    }
  (void) SetMagickResourceLimit(DiskResource,MagickResourceInfinity);
  limit=GetEnvironmentValue("MAGICK_DISK_LIMIT");
  if (limit != (char *) NULL)
    {
      (void) SetMagickResourceLimit(DiskResource,StringToSizeType(limit,100.0));
      limit=DestroyString(limit);
    }
  files=(-1);
#if defined(MAGICKCORE_HAVE_SYSCONF) && defined(_SC_OPEN_MAX)
  files=(ssize_t) sysconf(_SC_OPEN_MAX);
#endif
#if defined(MAGICKCORE_HAVE_GETRLIMIT) && defined(RLIMIT_NOFILE)
  if (files < 0)
    {
      struct rlimit
        resources;

      if (getrlimit(RLIMIT_NOFILE,&resources) != -1)
        files=(ssize_t) resources.rlim_cur;
  }
#endif
#if defined(MAGICKCORE_HAVE_GETDTABLESIZE) && defined(MAGICKCORE_POSIX_SUPPORT)
  if (files < 0)
    files=(ssize_t) getdtablesize();
#endif
  if (files < 0)
    files=64;
  (void) SetMagickResourceLimit(FileResource,MagickMax((size_t)
    (3*files/4),64));
  limit=GetEnvironmentValue("MAGICK_FILE_LIMIT");
  if (limit != (char *) NULL)
    {
      (void) SetMagickResourceLimit(FileResource,StringToSizeType(limit,
        100.0));
      limit=DestroyString(limit);
    }
  (void) SetMagickResourceLimit(ThreadResource,GetOpenMPMaximumThreads());
  limit=GetEnvironmentValue("MAGICK_THREAD_LIMIT");
  if (limit != (char *) NULL)
    {
      (void) SetMagickResourceLimit(ThreadResource,StringToSizeType(limit,
        100.0));
      limit=DestroyString(limit);
    }
  (void) SetMagickResourceLimit(ThrottleResource,0);
  limit=GetEnvironmentValue("MAGICK_THROTTLE_LIMIT");
  if (limit != (char *) NULL)
    {
      (void) SetMagickResourceLimit(ThrottleResource,StringToSizeType(limit,
        100.0));
      limit=DestroyString(limit);
    }
  (void) SetMagickResourceLimit(TimeResource,MagickResourceInfinity);
  limit=GetEnvironmentValue("MAGICK_TIME_LIMIT");
  if (limit != (char *) NULL)
    {
      (void) SetMagickResourceLimit(TimeResource,StringToSizeType(limit,100.0));
      limit=DestroyString(limit);
    }
  return(MagickTrue);
}

/*
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%                                                                             %
%                                                                             %
%                                                                             %
+   R e s o u r c e C o m p o n e n t T e r m i n u s                         %
%                                                                             %
%                                                                             %
%                                                                             %
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%
%  ResourceComponentTerminus() destroys the resource component.
%
%  The format of the ResourceComponentTerminus() method is:
%
%      ResourceComponentTerminus(void)
%
*/
MagickPrivate void ResourceComponentTerminus(void)
{
  if (resource_semaphore == (SemaphoreInfo *) NULL)
    resource_semaphore=AcquireSemaphoreInfo();
  LockSemaphoreInfo(resource_semaphore);
  if (temporary_resources != (SplayTreeInfo *) NULL)
    temporary_resources=DestroySplayTree(temporary_resources);
  if (random_info != (RandomInfo *) NULL)
    random_info=DestroyRandomInfo(random_info);
  UnlockSemaphoreInfo(resource_semaphore);
  RelinquishSemaphoreInfo(&resource_semaphore);
}

/*
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%                                                                             %
%                                                                             %
%                                                                             %
%   S e t M a g i c k R e s o u r c e L i m i t                               %
%                                                                             %
%                                                                             %
%                                                                             %
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%
%  SetMagickResourceLimit() sets the limit for a particular resource.
%
%  The format of the SetMagickResourceLimit() method is:
%
%      MagickBooleanType SetMagickResourceLimit(const ResourceType type,
%        const MagickSizeType limit)
%
%  A description of each parameter follows:
%
%    o type: the type of resource.
%
%    o limit: the maximum limit for the resource.
%
*/

MagickExport MagickBooleanType SetMagickResourceLimit(const ResourceType type,
  const MagickSizeType limit)
{
  char
    *value;

  if (resource_semaphore == (SemaphoreInfo *) NULL)
    resource_semaphore=AcquireSemaphoreInfo();
  LockSemaphoreInfo(resource_semaphore);
  value=(char *) NULL;
  switch (type)
  {
    case WidthResource:
    {
      resource_info.width_limit=limit;
      value=GetPolicyValue("resource:width");
      if (value != (char *) NULL)
        resource_info.width_limit=MagickMin(limit,StringToSizeType(value,
          100.0));
      break;
    }
    case HeightResource:
    {
      resource_info.height_limit=limit;
      value=GetPolicyValue("resource:height");
      if (value != (char *) NULL)
        resource_info.height_limit=MagickMin(limit,StringToSizeType(value,
          100.0));
      break;
    }
    case AreaResource:
    {
      resource_info.area_limit=limit;
      value=GetPolicyValue("resource:area");
      if (value != (char *) NULL)
        resource_info.area_limit=MagickMin(limit,StringToSizeType(value,100.0));
      break;
    }
    case MemoryResource:
    {
      resource_info.memory_limit=limit;
      value=GetPolicyValue("resource:memory");
      if (value != (char *) NULL)
        resource_info.memory_limit=MagickMin(limit,StringToSizeType(value,
          100.0));
      break;
    }
    case MapResource:
    {
      resource_info.map_limit=limit;
      value=GetPolicyValue("resource:map");
      if (value != (char *) NULL)
        resource_info.map_limit=MagickMin(limit,StringToSizeType(value,100.0));
      break;
    }
    case DiskResource:
    {
      resource_info.disk_limit=limit;
      value=GetPolicyValue("resource:disk");
      if (value != (char *) NULL)
        resource_info.disk_limit=MagickMin(limit,StringToSizeType(value,100.0));
      break;
    }
    case FileResource:
    {
      resource_info.file_limit=limit;
      value=GetPolicyValue("resource:file");
      if (value != (char *) NULL)
        resource_info.file_limit=MagickMin(limit,StringToSizeType(value,100.0));
      break;
    }
    case ThreadResource:
    {
      resource_info.thread_limit=limit;
      value=GetPolicyValue("resource:thread");
      if (value != (char *) NULL)
        resource_info.thread_limit=MagickMin(limit,StringToSizeType(value,
          100.0));
      if (resource_info.thread_limit > GetOpenMPMaximumThreads())
        resource_info.thread_limit=GetOpenMPMaximumThreads();
      else
        if (resource_info.thread_limit == 0)
          resource_info.thread_limit=1;
      break;
    }
    case ThrottleResource:
    {
      resource_info.throttle_limit=limit;
      value=GetPolicyValue("resource:throttle");
      if (value != (char *) NULL)
        resource_info.throttle_limit=MagickMax(limit,StringToSizeType(value,
          100.0));
      break;
    }
    case TimeResource:
    {
      resource_info.time_limit=limit;
      value=GetPolicyValue("resource:time");
      if (value != (char *) NULL)
        resource_info.time_limit=MagickMin(limit,StringToSizeType(value,100.0));
      ResetPixelCacheEpoch();
      break;
    }
    default:
      break;
  }
  if (value != (char *) NULL)
    value=DestroyString(value);
  UnlockSemaphoreInfo(resource_semaphore);
  return(MagickTrue);
}
