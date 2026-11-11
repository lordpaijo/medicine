#ifndef COMPAT_H
#define COMPAT_H

#if defined(_MSC_VER)
  #define PLAT_TLS __declspec(thread)
#elif defined(__GNUC__)
  #define PLAT_TLS __thread
#else
  #define PLAT_TLS _Thread_local
#endif

#if defined(__GNUC__)
  #define PRINTF_LIKE(fmt, args) __attribute__((format(printf, fmt, args)))
#else
  #define PRINTF_LIKE(fmt, args)
#endif

#endif
