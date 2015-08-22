#ifndef __orbment_defines_h__
#define __orbment_defines_h__

#if __GNUC__
#  define PLOG_ATTR(x, y) __attribute__((format(printf, x, y)))
#  define PNONULL __attribute__((nonnull))
#  define PNONULLV(...) __attribute__((nonnull(__VA_ARGS__)))
#  define PPURE __attribute__((pure))
#  define PCONST __attribute__((const))
#else
#  define PLOG_ATTR(x, y)
#  define PNONULL
#  define PNONULLV
#  define PPURE
#  define PCONST
#endif

#endif /* __orbment_defines_h__ */
