include_directories(src)
add_library(chck_orbment STATIC
   src/chck/pool/pool.c
   src/chck/string/string.c
   src/chck/dl/dl.c
   src/chck/lut/lut.c
   src/chck/xdg/xdg.c
   src/chck/thread/queue/queue.c)
