#ifndef SCOPE_LOCAL_H
#define SCOPE_LOCAL_H
/* Each software painter owns its raster state and corner caches. */
#ifdef _MSC_VER
#define SCOPE_LOCAL __declspec(thread)
#else
#define SCOPE_LOCAL _Thread_local
#endif
#endif
