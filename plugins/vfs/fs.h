#ifndef _fs_h_
#define _fs_h_

#include <pi9.h>
#include <chck/lut/lut.h>
#include <chck/pool/pool.h>

#define M(m) (m & 3)

#ifndef offsetof
#  if __GNUC__
#     define offsetof(st, m) __builtin_offsetof(st, m)
#  else
#     define offsetof(st, m) ((size_t)(&((st *)0)->m))
#  endif
#endif

static const size_t NONODE = (size_t)~0;

struct fs {
   struct chck_hash_table fids;
   struct chck_pool nodes;
   size_t root;
};

struct node {
   void *userdata;
   struct pi9_stat stat;
   struct chck_iter_pool childs;
   size_t parent;
   uint8_t omode;
   bool open;

   struct node_procs {
      bool (*read)(void *arg, uint16_t tag, uint32_t fid, uint64_t offset, uint32_t count, void *stream);
      bool (*write)(void *arg, uint16_t tag, uint32_t fid, uint64_t offset, uint32_t count, const void *data);
      void (*clunk)(void *arg, uint32_t fid);
      uint64_t (*size)(void *arg);
   } procs;
};

struct pi9_procs pi9_procs;

enum {
   NONE = 0,
   ROOT,
};

bool node_init(struct node *node, struct node_procs *procs);
void node_release(struct node *node);
struct node* get_node(struct fs *fs, size_t node);
size_t fs_add_node(struct fs *fs, struct node *node);
void fs_remove_node(struct fs *fs, struct node *node);
size_t fs_add_node_linked(struct fs *fs, struct node *node, size_t parent);
bool fs_init(struct fs *fs, uint32_t initial_nodes);
void fs_release(struct fs *fs);
int announce_unix(const char *file);

#endif /* _fs_h_ */
