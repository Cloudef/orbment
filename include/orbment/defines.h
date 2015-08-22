#ifndef __orbment_defines_h__
#define __orbment_defines_h__

#if __GNUC__
#  define PLOG_ATTR(x, y) __attribute__((format(printf, x, y)))
#  define PNONULL __attribute__((nonnull))
#  define PNONULLV(...) __attribute__((nonnull(__VA_ARGS__)))
#else
#  define PLOG_ATTR(x, y)
#  define PNONULL
#  define PNONULLV
#endif

#endif /* __orbment_defines_h__ */
