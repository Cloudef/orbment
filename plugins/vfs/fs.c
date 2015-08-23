#include <orbment/plugin.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <unistd.h>
#include <time.h>
#include <assert.h>
#include "fs.h"

PPURE struct node*
get_node(struct fs *fs, size_t node)
{
   return (node == NONODE ? NULL : chck_pool_get(&fs->nodes, node));
}

static bool
unlink_nodes(struct node *parent, struct node *child)
{
   if (!child)
      return false;

   if (parent) {
      size_t *n;
      chck_iter_pool_for_each(&parent->childs, n) {
         if (*n != child->stat.qid.path)
            continue;

         chck_iter_pool_remove(&parent->childs, _I - 1);
         break;
      }
   }

   child->parent = NONODE;
   return true;
}

static void
unlink_childs(struct fs *fs, struct node *parent)
{
   if (!parent)
      return;

   size_t *c;
   chck_iter_pool_for_each(&parent->childs, c)
      unlink_nodes(parent, get_node(fs, *c));

   chck_iter_pool_release(&parent->childs);
}

static bool
link_nodes(struct fs *fs, struct node *parent, struct node *child)
{
   assert(fs);

   if (!parent || !child)
      return false;

   if (child->parent != NONODE && !unlink_nodes(get_node(fs, child->parent), child))
      return false;

   if (child->stat.qid.path != NONODE && !chck_iter_pool_push_back(&parent->childs, &child->stat.qid.path))
      return false;

   child->parent = parent->stat.qid.path;
   return true;
}

static inline struct node*
fid_to_node(struct fs *fs, uint32_t fid, size_t *n)
{
   if (n) *n = 0;

   if (fid == PI9_NOFID)
      return NULL;

   size_t *i;
   if (!(i = chck_hash_table_get(&fs->fids, fid)))
      return NULL;

   if (n) *n = *i;
   return get_node(fs, *i);
}

static bool
set_fid(struct fs *fs, uint32_t fid, const size_t *node)
{
   return chck_hash_table_set(&fs->fids, fid, node);
}

static bool
clunk_fid(struct fs *fs, uint32_t fid)
{
   struct node *f;
   if ((f = fid_to_node(fs, fid, NULL))) {
      if (f->procs.clunk)
         f->procs.clunk(f->userdata, fid);
      if (f->omode & PI9_ORCLOSE) {
         fs_remove_node(fs, f);
         f = NULL; // ↑ f is garbage after this
      } else {
         f->omode = 0;
         f->open = false;
      }
   }

   return set_fid(fs, fid, NULL);
}

bool
node_init(struct node *node, struct node_procs *procs)
{
   memset(node, 0, sizeof(struct node));

   if (procs)
      memcpy(&node->procs, procs, sizeof(struct node_procs));

   node->stat.qid.path = node->parent = NONODE;
   return chck_iter_pool(&node->childs, 4, 0, sizeof(size_t));
}

void
node_release(struct node *node)
{
   if (!node)
      return;

   pi9_stat_release(&node->stat);
   chck_iter_pool_release(&node->childs);
}

size_t
fs_add_node(struct fs *fs, struct node *node)
{
   assert(fs && node);

   size_t n;
   struct node *p;
   if (!(p = chck_pool_add(&fs->nodes, node, &n)))
      return NONODE;

   return (p->stat.qid.path = n);
}

void
fs_remove_node(struct fs *fs, struct node *node)
{
   if (!node)
      return;

   unlink_childs(fs, node);
   unlink_nodes(get_node(fs, node->parent), node);
   node_release(node);

   if (node->stat.qid.path != NONODE) {
      chck_pool_remove(&fs->nodes, node->stat.qid.path);
      node = NULL; // ↑ node pointer is garbage after this
   }
}

size_t
fs_add_node_linked(struct fs *fs, struct node *node, size_t parent)
{
   assert(fs && node);

   size_t n;
   if ((n = fs_add_node(fs, node)) == NONODE)
      return NONODE;

   if (!link_nodes(fs, chck_pool_get(&fs->nodes, parent), get_node(fs, n))) {
      fs_remove_node(fs, get_node(fs, n));
      return NONODE;
   }

   return n;
}

void
fs_release(struct fs *fs)
{
   if (!fs)
      return;

   chck_pool_for_each_call(&fs->nodes, node_release);
   chck_hash_table_release(&fs->fids);
   chck_pool_release(&fs->nodes);
}

bool
fs_init(struct fs *fs, uint32_t initial_nodes)
{
   assert(fs);
   memset(fs, 0, sizeof(struct fs));

   if (!chck_hash_table(&fs->fids, PI9_NOFID, 128, sizeof(size_t)))
      goto error0;

   chck_hash_table_uint_algorithm(&fs->fids, chck_incremental_uint_hash);

   if (!chck_pool(&fs->nodes, 32, initial_nodes, sizeof(struct node)))
      goto error0;

   struct node root;
   if (!node_init(&root, NULL))
      goto error0;

   root.stat = (struct pi9_stat) {
      .type = ROOT,
      .dev = 0,
      .qid = { PI9_QTDIR, 0, 0 },
      .mode = PI9_DMDIR | 0700,
      .atime = time(NULL),
      .mtime = time(NULL),
      .length = 0, // dirs are always 0
      .name = { NULL, 0, false },
      .uid = { NULL, 0, false },
      .gid = { NULL, 0, false },
      .muid = { NULL, 0, false },
   };

   if ((fs->root = fs_add_node(fs, &root)) == NONODE)
      goto error1;

   return true;

error1:
   node_release(&root);
error0:
   fs_release(fs);
   return false;
}

static bool
cb_read_qtdir(struct pi9 *pi9, struct node *node, uint16_t tag, uint64_t offset, uint32_t count)
{
   (void)offset, (void)count;

   struct pi9_reply reply;
   pi9_reply_start(&reply, tag, pi9->stream);

   // For directories, read returns an integral number of directory entries exactly as in stat (see stat(5)),
   // one for each member of the directory. The read request message must have offset equal to zero or the
   // value of offset in the previous read on the directory, plus the number of bytes returned in the previous read.
   // In other words, seeking other than to the beginning is illegal in a directory (see seek(2)).

   if (offset == 0) {
      struct pi9_stat stats[2];
      memcpy(&stats[0], &node->stat, sizeof(struct pi9_stat));
      memcpy(&stats[1], &node->stat, sizeof(struct pi9_stat));
      stats[0].name = (struct pi9_string){ ".", 1, false };
      stats[1].name = (struct pi9_string){ "..", 2, false };

      for (uint32_t i = 0; i < 2; ++i) {
         if (!pi9_write_stat(&stats[i], pi9->stream))
            return false;
      }

      for (size_t i = 0; i < node->childs.items.count; ++i) {
         size_t *n = chck_iter_pool_get(&node->childs, i);
         struct node *c = get_node(pi9->userdata, *n);
         assert(c);

         // update size from callback
         if (c->procs.size)
            c->stat.length = c->procs.size(c->userdata);

         if (!pi9_write_stat(&c->stat, pi9->stream))
            return false;
      }
   }

   return pi9_reply_send(&reply, pi9->fd, pi9->stream);
}

static bool
cb_attach(struct pi9 *pi9, uint16_t tag, uint32_t fid, uint32_t afid, const struct pi9_string *uname, const struct pi9_string *aname, struct pi9_qid **qid)
{
   (void)tag, (void)uname, (void)aname;

   // if afid == NOFID then no authentication is requested
   struct node *af = NULL;
   if (afid != PI9_NOFID && !(af = fid_to_node(pi9->userdata, afid, NULL)))
      goto err_not_allowed; // the node for authentication does not exist

   // asked for authentication but we don't support it
   if (af)
      goto err_no_auth;

   // fid that is in use cannot be used to access root node
   if (fid_to_node(pi9->userdata, fid, NULL))
      goto err_not_allowed;

   struct fs *fs = pi9->userdata;
   if (!set_fid(fs, fid, &fs->root))
      goto err_oom;

   struct node *f;
   if (!(f = get_node(fs, fs->root)))
      goto err_nofid;

   *qid = &f->stat.qid;
   return true;

err_nofid:
   pi9_write_error(tag, PI9_ERR_NO_FID, pi9->stream);
   return false;
err_oom:
   pi9_write_error(tag, PI9_ERR_OUT_OF_MEMORY, pi9->stream);
   return false;
err_not_allowed:
   pi9_write_error(tag, PI9_ERR_NOT_ALLOWED, pi9->stream);
   return false;
err_no_auth:
   pi9_write_error(tag, PI9_ERR_NO_AUTH, pi9->stream);
   return false;
}

static bool
cb_flush(struct pi9 *pi9, uint16_t tag, uint16_t oldtag)
{
   (void)pi9, (void)tag, (void)oldtag;

   // When the response to a request is no longer needed, such as when a user interrupts a process doing a read(2), a
   // Tflush request is sent to the server to purge the pending response. The message being flushed is identified by oldtag.
   // The semantics of flush depends on messages arriving in order.

   // The server may respond to the pending request before responding to the Tflush.
   // It is possible for a client to send multiple Tflush messages for a particular pending request.
   // Each subsequent Tflush must contain as oldtag the tag of the pending request (not a previous Tflush).
   // Should multiple Tflushes be received for a pending request, they must be answered in order.
   // A Rflush for any of the multiple Tflushes implies an answer for all previous ones. Therefore,
   // should a server receive a request and then multiple flushes for that request, it need respond only to the last flush.

   return true;
}

static bool
cb_walk(struct pi9 *pi9, uint16_t tag, uint32_t fid, uint32_t newfid, uint16_t nwname, const struct pi9_string *walks, struct pi9_qid **qids, uint16_t *out_nwqid)
{
   (void)tag;

   size_t index;
   struct node *f;
   if (!(f = fid_to_node(pi9->userdata, fid, &index)))
      goto err_nofid;

   // The walk request carries as arguments an existing fid and a proposed newfid
   // (which must not be in use unless it is the same as fid)
   if (fid != newfid && fid_to_node(pi9->userdata, newfid, NULL))
      goto err_fid_in_use;

   struct node *wnode = f;
   for (uint32_t i = 0; i < nwname; ++i) {
      // The fid must represent a directory unless zero path name elements are specified.
      if (wnode->stat.qid.type != PI9_QTDIR) {
         // Otherwise, the walk will return an Rwalk message containing nwqid qids corresponding, in order,
         // to the files that are visited by the nwqid successful elementwise walks;
         break;
      }

      // The name ``..'' (dot-dot) represents the parent directory.
      // Single (dot) is not used in 9p protocol
      if (pi9_string_eq_cstr(&walks[i], "..")) {
         struct node *n;
         if (wnode->stat.type == ROOT) {
            // A walk of the name ``..'' in the root directory of a server is equivalent to a walk with no name elements.
            n = wnode;
         } else if (!(n = get_node(pi9->userdata, wnode->parent))) {
            break;
         }

         qids[*out_nwqid] = &n->stat.qid;
         index = (wnode->parent != NONODE ? wnode->parent : index);
         wnode = n;
         (*out_nwqid)++;
      } else {
         size_t *c;
         chck_iter_pool_for_each(&wnode->childs, c) {
            struct node *n;
            if (!(n = get_node(pi9->userdata, *c)) || !pi9_string_eq(&walks[i], &n->stat.name))
               continue;

            qids[*out_nwqid] = &n->stat.qid;
            index = *c;
            wnode = n;
            (*out_nwqid)++;
            break;
         }
      }
   }

   // If the first element cannot be walked for any reason, Rerror is returned.
   if (nwname > 0 && *out_nwqid == 0)
      goto err_not_directory;

   // Index is only affected if equal
   if (*out_nwqid != nwname)
      index = NONODE;

   // Always same when affected, otherwise unaffected (index == NONODE)
   assert(*out_nwqid == nwname || index == NONODE);

   // It is legal for nwname to be zero, in which case newfid will represent the same file as fid
   if (index != NONODE && !set_fid(pi9->userdata, newfid, &index))
      goto err_oom;

   return true;

err_nofid:
   pi9_write_error(tag, PI9_ERR_NO_FID, pi9->stream);
   return false;
err_fid_in_use:
   pi9_write_error(tag, PI9_ERR_FID_IN_USE, pi9->stream);
   return false;
err_not_directory:
   pi9_write_error(tag, PI9_ERR_NOT_DIRECTORY, pi9->stream);
   return false;
err_oom:
   pi9_write_error(tag, PI9_ERR_OUT_OF_MEMORY, pi9->stream);
   return false;
}

static bool
cb_open(struct pi9 *pi9, uint16_t tag, uint32_t fid, uint8_t mode, struct pi9_qid **out_qid, uint32_t *out_iounit)
{
   (void)tag, (void)out_iounit;

   // if mode has the OTRUNC (0x10) bit set, the file is to be truncated, which requires write permission
   // (if the file is append-only, and permission is granted, the open succeeds but the file will not be truncated);

   // if the mode has the ORCLOSE (0x40) bit set, the file is to be removed when the fid is clunked,
   // which requires permission to remove the file from its directory.

   // It is illegal to write a directory, truncate it, or attempt to remove it on close.

   // If the file is marked for exclusive use (see stat(5)), only one client can have the file open at any time.
   // That is, after such a file has been opened, further opens will fail until fid has been clunked.

   struct node *f;
   if (!(f = fid_to_node(pi9->userdata, fid, NULL)))
      goto err_nofid;

   // It is an error for either of these messages if the fid is already the product of a successful open or create message.
   if (f->open)
      goto err_not_allowed;

   // The iounit field returned by open and create may be zero. If it is not,
   // it is the maximum number of bytes that are guaranteed to be read from or written to the file
   // without breaking the I/O transfer into multiple 9P messages.
   // (out_iounit is by default 0)

   f->omode = mode;
   f->open = true;
   *out_qid = &f->stat.qid;
   return true;

err_nofid:
   pi9_write_error(tag, PI9_ERR_NO_FID, pi9->stream);
   return false;
err_not_allowed:
   pi9_write_error(tag, PI9_ERR_NOT_ALLOWED, pi9->stream);
   return false;
}

static bool
cb_create(struct pi9 *pi9, uint16_t tag, uint32_t fid, const struct pi9_string *name, uint32_t perm, uint8_t mode, struct pi9_qid **out_qid, uint32_t *out_iounit)
{
   (void)tag, (void)perm, (void)mode, (void)out_qid, (void)out_iounit;

   // The create request asks the file server to create a new file with the name supplied,
   // in the directory (dir) represented by fid, and requires write permission in the directory.
   // The owner of the file is the implied user id of the request, the group of the file is the same as dir,
   // and the permissions are the value of ```perm & (~0666 | (dir.perm & 0666))``` if a regular file is being created
   // and ```perm & (~0777 | (dir.perm & 0777))``` if a directory is being created. This means, for example,
   // that if the create allows read permission to others, but the containing directory does not,
   // then the created file will not allow others to read the file.

   // Finally, the newly created file is opened according to mode, and fid will represent the newly opened file.
   // Mode is not checked against the permissions in perm. The qid for the new file is returned with the create reply message.

   // Directories are created by setting the DMDIR bit (0x80000000) in the perm.

   struct node *f;
   if (!(f = fid_to_node(pi9->userdata, fid, NULL)))
      goto err_nofid;

   // It is an error for either of these messages if the fid is already the product of a successful open or create message.
   if (f->open || f->stat.qid.type != PI9_QTDIR)
      goto err_not_allowed;

   // An attempt to create a file in a directory where the given name already exists will be rejected
   size_t *c;
   chck_iter_pool_for_each(&f->childs, c) {
      struct node *n = get_node(pi9->userdata, *c);
      assert(n);

      if (pi9_string_eq(&n->stat.name, name))
         goto err_not_allowed;
   }

   // The iounit field returned by open and create may be zero. If it is not,
   // it is the maximum number of bytes that are guaranteed to be read from or written to the file
   // without breaking the I/O transfer into multiple 9P messages.
   // (out_iounit is by default 0)

   // FIXME: creation code here
   pi9_write_error(tag, PI9_ERR_NO_FID, pi9->stream);
   return false;

err_nofid:
   pi9_write_error(tag, PI9_ERR_NO_FID, pi9->stream);
   return false;
err_not_allowed:
   pi9_write_error(tag, PI9_ERR_NOT_ALLOWED, pi9->stream);
   return false;
}

static bool
cb_read(struct pi9 *pi9, uint16_t tag, uint32_t fid, uint64_t offset, uint32_t count)
{
   (void)tag;

   struct node *f;
   if (!(f = fid_to_node(pi9->userdata, fid, NULL)))
      goto err_nofid;

   if (M(f->omode) != PI9_OREAD)
      goto err_not_allowed;

   if (f->stat.qid.type == PI9_QTDIR && !cb_read_qtdir(pi9, f, tag, offset, count)) {
      goto err_write;
   } else if (f->procs.read && !f->procs.read(f->userdata, tag, fid, offset, count, pi9->stream))
      goto err_write;

   return true;

err_write:
   pi9_write_error(tag, PI9_ERR_WRITE, pi9->stream);
   return false;
err_nofid:
   pi9_write_error(tag, PI9_ERR_NO_FID, pi9->stream);
   return false;
err_not_allowed:
   pi9_write_error(tag, PI9_ERR_NOT_ALLOWED, pi9->stream);
   return false;
}

static bool
cb_write(struct pi9 *pi9, uint16_t tag, uint32_t fid, uint64_t offset, uint32_t count, const void *data)
{
   (void)tag;

   struct node *f;
   if (!(f = fid_to_node(pi9->userdata, fid, NULL)))
      goto err_nofid;

   // Directories may not be written.
   if (f->stat.qid.type == PI9_QTDIR || M(f->omode) != PI9_OWRITE)
      goto err_not_allowed;

   // The write request asks that count bytes of data be recorded in the file identified by fid,
   // which must be opened for writing, starting offset bytes after the beginning of the file.
   // If the file is append-only, the data will be placed at the end of the file regardless of offset.

   if (f->procs.write && !f->procs.write(f->userdata, tag, fid, offset, count, data))
      goto err_write;

   return true;

err_write:
   pi9_write_error(tag, PI9_ERR_WRITE, pi9->stream);
   return false;
err_nofid:
   pi9_write_error(tag, PI9_ERR_NO_FID, pi9->stream);
   return false;
err_not_allowed:
   pi9_write_error(tag, PI9_ERR_NOT_ALLOWED, pi9->stream);
   return false;
}

static bool
cb_clunk(struct pi9 *pi9, uint16_t tag, uint32_t fid)
{
   (void)tag;

   if (!clunk_fid(pi9->userdata, fid))
      goto err_oom;

   // The actual file is not removed on the server unless the fid had been opened with ORCLOSE.
   // Once a fid has been clunked, the same fid can be reused in a new walk or attach request.
   // Even if the clunk returns an error, the fid is no longer valid.
   // A clunk message is generated by close and indirectly by other actions such as failed open calls.

   return true;

err_oom:
   pi9_write_error(tag, PI9_ERR_OUT_OF_MEMORY, pi9->stream);
   return false;
}

static bool
cb_remove(struct pi9 *pi9, uint16_t tag, uint32_t fid)
{
   (void)tag;

   struct node *f;
   if (!(f = fid_to_node(pi9->userdata, fid, NULL)))
      goto err_nofid;

   // The remove request asks the file server both to remove the file represented by fid and to clunk the fid, even if the remove fails.
   // This request will fail if the client does not have write permission in the parent directory.

   // It is correct to consider remove to be a clunk with the side effect of removing the file if permissions allow.

   // If a file has been opened as multiple fids, possibly on different connections, and one fid is used to remove the file,
   // whether the other fids continue to provide access to the file is implementation-defined.

   if (!clunk_fid(pi9->userdata, fid))
      goto err_oom;

   if (f->stat.type == ROOT)
      goto err_not_allowed;

   fs_remove_node(pi9->userdata, f);
   return true;

err_nofid:
   pi9_write_error(tag, PI9_ERR_NO_FID, pi9->stream);
   return false;
err_oom:
   pi9_write_error(tag, PI9_ERR_OUT_OF_MEMORY, pi9->stream);
   return false;
err_not_allowed:
   pi9_write_error(tag, PI9_ERR_NOT_ALLOWED, pi9->stream);
   return false;
}

static bool
cb_stat(struct pi9 *pi9, uint16_t tag, uint32_t fid, struct pi9_stat **out_stat)
{
   (void)tag;

   // The stat request requires no special permissions.

   struct node *f;
   if (!(f = fid_to_node(pi9->userdata, fid, NULL)))
      goto err_nofid;

   // update size from callback
   if (f->procs.size)
      f->stat.length = f->procs.size(f->userdata);

   *out_stat = &f->stat;
   return true;

err_nofid:
   pi9_write_error(tag, PI9_ERR_NO_FID, pi9->stream);
   return false;
}

static bool
cb_twstat(struct pi9 *pi9, uint16_t tag, uint32_t fid, const struct pi9_stat *stat)
{
   (void)tag;

   struct node *f;
   if (!(f = fid_to_node(pi9->userdata, fid, NULL)))
      goto err_nofid;

   // It is an error to attempt to set the length of a directory to a non-zero value, and servers may decide to reject
   // length changes for other reasons.
   if (f->stat.qid.type == PI9_QTDIR && stat->length != f->stat.length)
      goto err_not_allowed;

   // wstat request can avoid modifying some properties of the file by providing explicit ``don't touch'' values in the stat data
   // that is sent: zero-length strings for text values and the maximum unsigned value of appropriate size for integral values.
   // As a special case, if all the elements of the directory entry in a Twstat message are ``don't touch'' values, the server
   // may interpret it as a request to guarantee that the contents of the associated file are committed to stable storage
   // before the Rwstat message is returned.
   // (Consider the message to mean, ``make the state of the file exactly what it claims to be.'')

   struct pi9_stat wstat;
   memcpy(&wstat, stat, sizeof(wstat));
   if (wstat.type == (uint16_t)~0) wstat.type = f->stat.type;
   if (wstat.dev == (uint32_t)~0) wstat.dev = f->stat.dev;
   if (wstat.qid.type == (uint8_t)~0) wstat.qid.type = f->stat.qid.type;
   if (wstat.qid.vers == (uint32_t)~0) wstat.qid.vers = f->stat.qid.vers;
   if (wstat.qid.path == (uint64_t)~0) wstat.qid.path = f->stat.qid.path;
   if (wstat.mode == (uint32_t)~0) wstat.mode = f->stat.mode;
   if (wstat.atime == (uint32_t)~0) wstat.atime = f->stat.atime;
   if (wstat.mtime == (uint32_t)~0) wstat.mtime = f->stat.mtime;
   if (wstat.length == (uint64_t)~0) wstat.length = f->stat.length;

   struct pi9_string *fields[4] = { &wstat.name, &wstat.uid, &wstat.gid, &wstat.muid };
   struct pi9_string *fields2[4] = { &f->stat.name, &f->stat.uid, &f->stat.gid, &wstat.muid };
   for (uint32_t i = 0; i < 4; ++i) {
      if (fields[i]->size > 0)
         continue;

      pi9_string_set(fields[i], fields2[i], false);
   }

   // The length can be changed (affecting the actual length of the file) by anyone with write permission on the file.
   // of the file's current group. The directory bit cannot be changed by a wstat;
   // The mode and mtime can be changed by the owner of the file or the group leader the other defined permission and mode bits can.
   // The gid can be changed: by the owner if also a member of the new group;
   // or by the group leader of the file's current group if also leader of the new group
   // (see intro(5) for more information about permissions and users(6) for users and groups).
   // None of the other data can be altered by a wstat and attempts to change them will trigger an error.
   // In particular, it is illegal to attempt to change the owner of a file.
   // (These conditions may be relaxed when establishing the initial state of a file server; see fsconfig(8).)
   if (wstat.type != f->stat.type ||
       wstat.dev != f->stat.dev ||
       wstat.atime != f->stat.atime ||
       memcmp(&wstat.qid, &f->stat.qid, sizeof(wstat.qid)) ||
       !pi9_string_eq(&wstat.uid, &f->stat.uid) ||
       !pi9_string_eq(&wstat.muid, &f->stat.muid))
      goto err_not_allowed;

   if (wstat.name.size > 0 && !pi9_string_eq(&wstat.name, &f->stat.name)) {
      // The name can be changed by anyone with write permission in the parent directory;
      // it is an error to change the name to that of an existing file.
      struct node *p;
      if ((p = get_node(pi9->userdata, f->parent))) {
         size_t *c;
         chck_iter_pool_for_each(&p->childs, c) {
            struct node *n = get_node(pi9->userdata, *c);
            assert(n);

            if (pi9_string_eq(&n->stat.name, &wstat.name))
               goto err_not_allowed;
         }
      }
   }

   // ↑
   // Either all the changes in wstat request happen, or none of them does:
   // if the request succeeds, all changes were made; if it fails, none were.
   memcpy(&f->stat, &wstat, offsetof(struct pi9_stat, name));

   if (f->parent != NONODE && wstat.name.size > 0)
      pi9_string_set(&f->stat.name, &wstat.name, true); // this can fail in OOM, but oh well

   return true;

err_nofid:
   pi9_write_error(tag, PI9_ERR_NO_FID, pi9->stream);
   return false;
err_not_allowed:
   pi9_write_error(tag, PI9_ERR_NOT_ALLOWED, pi9->stream);
   return false;
}

struct pi9_procs pi9_procs = {
   .auth = NULL,
   .attach = cb_attach,
   .flush = cb_flush,
   .walk = cb_walk,
   .open = cb_open,
   .create = cb_create,
   .read = cb_read,
   .write = cb_write,
   .clunk = cb_clunk,
   .remove = cb_remove,
   .stat = cb_stat,
   .twstat = cb_twstat
};

static int
sock_unix(const char *address, struct sockaddr_un *sa, socklen_t *salen)
{
   assert(address && sa && salen);
   memset(sa, 0, sizeof(*sa));

   sa->sun_family = AF_UNIX;
   strncpy(sa->sun_path, address, sizeof(sa->sun_path));
   *salen = SUN_LEN(sa);

   int fd;
   if ((fd = socket(AF_UNIX, SOCK_STREAM, 0)) < 0)
      return -1;

   return fd;
}

int
announce_unix(const char *file)
{
   assert(file);

   int fd;
   socklen_t salen;
   struct sockaddr_un sa;
   if ((fd = sock_unix(file, &sa, &salen)) < 0)
      return -1;

   if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (int[]){1}, sizeof(int)) < 0)
      goto fail;

   unlink(file);
   if (bind(fd, (struct sockaddr*)&sa, salen) < 0)
      goto fail;

   chmod(file, S_IRWXU);
   if (listen(fd, 0) < 0)
      goto fail;

   return fd;

fail:
   close(fd);
   return -1;
}
