#include "log.h"
#include <stdio.h>
#include <stdarg.h>
#include <time.h>
#include <assert.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <wlc/wlc.h>
#include "plugin.h"

static struct {
   FILE *file;
} logger;

static inline void
log_timestamp(FILE *out)
{
   assert(out);

   struct timeval tv;
   struct tm *brokendown_time;
   gettimeofday(&tv, NULL);

   if (!(brokendown_time = localtime(&tv.tv_sec))) {
      fprintf(out, "[(NULL)localtime] ");
      return;
   }

   char string[128];
   static int cached_tm_mday;
   if (brokendown_time->tm_mday != cached_tm_mday) {
      strftime(string, sizeof(string), "%Y-%m-%d %Z", brokendown_time);
      fprintf(out, "Date: %s\n", string);
      cached_tm_mday = brokendown_time->tm_mday;
   }

   strftime(string, sizeof(string), "%H:%M:%S", brokendown_time);
   fprintf(out, "[%s.%03li] ", string, tv.tv_usec / 1000);
}

static inline void
cb_log(enum wlc_log_type type, const char *str)
{
   assert(str);
   type = (type == WLC_LOG_WAYLAND ? WLC_LOG_INFO : type);
   plog(0, type, "%s: %s", (type == WLC_LOG_WAYLAND ? "wayland" : "wlc"), str);
}

void
logv(enum plugin_log_type type, const char *prefix, const char *fmt, va_list ap)
{
   assert(fmt);

   FILE *out = (logger.file ? logger.file : stderr);

   if (out != stderr && out != stdout)
      log_timestamp(out);

   switch (type) {
      case PLOG_WARN:
         fprintf(out, "(WARN) ");
         break;
      case PLOG_ERROR:
         fprintf(out, "(ERROR) ");
         break;

      default: break;
   }

   if (prefix)
      fprintf(out, "%s: ", prefix);

   vfprintf(out, fmt, ap);
   fprintf(out, "\n");
   fflush(out);
}

void
log_set_file(const char *path)
{
   logger.file = (path ? fopen(path, "a") : NULL);
}

void
log_open(void)
{
   wlc_log_set_handler(cb_log);
}

void
log_close(void)
{
   if (logger.file && logger.file != stdout && logger.file != stderr)
      fclose(logger.file);
}

void
log_backtrace(void)
{
   if (clearenv() != 0)
      return;

   /* GDB */
#if defined(__linux__) || defined(__APPLE__)
   pid_t child_pid = fork();

#if HAS_YAMA_PRCTL
   /* tell yama that we allow our child_pid to trace our process */
   if (child_pid > 0) {
      if (!prctl(PR_GET_DUMPABLE)) {
         plog(0, PLOG_WARN, "Compositor binary is suid/sgid, most likely since you are running from TTY.");
         plog(0, PLOG_WARN, "Kernel ptracing security policy does not allow attaching to suid/sgid processes.");
         plog(0, PLOG_WARN, "If you don't get backtrace below, try `setcap cap_sys_ptrace=eip gdb` temporarily.");
      }
      prctl(PR_SET_DUMPABLE, 1);
      prctl(PR_SET_PTRACER, child_pid);
   }
#endif

   if (child_pid < 0) {
      plog(0, PLOG_ERROR, "Fork failed for gdb backtrace");
   } else if (child_pid == 0) {
      /*
       * NOTE: gdb-7.8 does not seem to work with this,
       *       either downgrade to 7.7 or use gdb from master.
       */

      /* sed -n '/bar/h;/bar/!H;$!b;x;p' (another way, if problems) */
      char buf[255];
      const int fd = fileno((logger.file ? logger.file : stderr));
      snprintf(buf, sizeof(buf) - 1, "gdb -p %d -n -batch -ex bt 2>/dev/null | sed -n '/<signal handler/{n;x;b};H;${x;p}' 1>&%d", getppid(), fd);
      execl("/bin/sh", "/bin/sh", "-c", buf, NULL);
      plog(0, PLOG_ERROR, "Failed to launch gdb for backtrace");
      _exit(EXIT_FAILURE);
   } else {
      waitpid(child_pid, NULL, 0);
   }
#endif
}
