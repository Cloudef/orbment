#include "signals.h"
#include <stdlib.h>
#include <signal.h>
#include <wlc/wlc.h>
#include "plugin.h"
#include "log.h"

#if defined(__linux__)
#  include <linux/version.h>
#  if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 4, 0)
#     include <sys/prctl.h> /* for yama */
#     define HAS_YAMA_PRCTL 1
#  endif
#endif

#if defined(__linux__) && defined(__GNUC__)
#  include <fenv.h>
int feenableexcept(int excepts);
#endif

#if (defined(__APPLE__) && (defined(__i386__) || defined(__x86_64__)))
#  define OSX_SSE_FPE
#  include <xmmintrin.h>
#endif

#ifndef NDEBUG

static void
fpehandler(int signal)
{
   (void)signal;
   plog(0, PLOG_ERROR, "SIGFPE signal received");
   abort();
}

static void
fpesetup(struct sigaction *action)
{
#if defined(__linux__) || defined(_WIN32) || defined(OSX_SSE_FPE)
   action->sa_handler = fpehandler;
   sigaction(SIGFPE, action, NULL);
#  if defined(__linux__) && defined(__GNUC__)
   feenableexcept(FE_DIVBYZERO | FE_INVALID | FE_OVERFLOW);
#  endif /* defined(__linux__) && defined(__GNUC__) */
#  if defined(OSX_SSE_FPE)
   return; /* causes issues */
   /* OSX uses SSE for floating point by default, so here
    * use SSE instructions to throw floating point exceptions */
   _MM_SET_EXCEPTION_MASK(_MM_MASK_MASK & ~(_MM_MASK_OVERFLOW | _MM_MASK_INVALID | _MM_MASK_DIV_ZERO));
#  endif /* OSX_SSE_FPE */
#  if defined(_WIN32) && defined(_MSC_VER)
   _controlfp_s(NULL, 0, _MCW_EM); /* enables all fp exceptions */
   _controlfp_s(NULL, _EM_DENORMAL | _EM_UNDERFLOW | _EM_INEXACT, _MCW_EM); /* hide the ones we don't care about */
#  endif /* _WIN32 && _MSC_VER */
#endif
}

static void
backtrace(int signal)
{
   (void)signal;
   log_backtrace();

   /* SIGABRT || SIGSEGV */
   exit(EXIT_FAILURE);
}

#endif /* NDEBUG */

static void
sigterm(int signal)
{
   (void)signal;
   plog(0, PLOG_INFO, "Got %s", (signal == SIGTERM ? "SIGTERM" : "SIGINT"));
   wlc_terminate();
}

void
signals_setup_debug(void)
{
#ifndef NDEBUG
   {
      struct sigaction action = {
         .sa_handler = backtrace
      };

      sigaction(SIGABRT, &action, NULL);
      sigaction(SIGSEGV, &action, NULL);
      fpesetup(&action);
   }
#endif
}

void
signals_setup(void)
{
   {
      struct sigaction action = {
         .sa_handler = SIG_DFL,
         .sa_flags = SA_NOCLDWAIT
      };

      // do not care about childs
      sigaction(SIGCHLD, &action, NULL);
   }

   {
      struct sigaction action = {
         .sa_handler = sigterm,
      };

      sigaction(SIGTERM, &action, NULL);
      sigaction(SIGINT, &action, NULL);
   }
}
