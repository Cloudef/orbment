include_directories(src)

add_definitions(-D_DEFAULT_SOURCE)

add_library(chck_orbment STATIC
   src/chck/pool/pool.c
   src/chck/string/string.c
   src/chck/dl/dl.c
   src/chck/lut/lut.c
   src/chck/xdg/xdg.c
   src/chck/thread/queue/queue.c)

set_target_properties(chck_orbment PROPERTIES C_STANDARD 99 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF POSITION_INDEPENDENT_CODE ON)
