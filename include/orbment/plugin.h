#ifndef __orbment_plugin_h__
#define __orbment_plugin_h__

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

/**
 * Struct abstracting exported method from plugin.
 * Do not use this struct directly, but instead the REGISTER_METHOD macro.
 */
struct method {
   void *function;
   struct method_info {
      const char *name, *signature;
   } info;
   bool deprecated;
};

/**
 * Use this instead of pointers when passing functions between plugins.
 * The other plugin can sanity check the function signature.
 * See also the FUN macro.
 */
struct function {
   void *function;
   const char *signature;
};

/**
 * Struct which statically allocated reference should be returned by plugin_register function.
 * Used for filling information and functionality of the plugin.
 *
 * XXX: Set some good standard for plugins that provide common functionaly.
 *      For example status bar plugins should provide 'bar' plugin or something.
 *      Plugin that depends on OpenGL backend in wlc (for example desktop cube) should require on 'opengl' meta-plugin.
 */
struct plugin_info {
   /**
    * Name of the plugin.
    */
   const char *name;

   /**
    * Description of the plugin.
    */
   const char *description;

   /**
    * Version of the plugin.
    * XXX: Semantic version?
    * XXX: We don't relly use this information for anything yet. Should we? Just for metadata right now.
    */
   const char *version;

   /**
    * Use REGISTER_METHOD macro to register each method the plugin exports for others.
    * Zero-terminated array of methods (struct method)
    */
   const struct method *methods;

   /**
    * Provides given plugins in addition to the name of this plugin.
    * May be NULL, if no additional provides are needed.
    * Zero-terminated array of pointers to char arrays.
    */
   const char **provides;

   /**
    * Conflicts with the given plugins in addition to the name of this plugin.
    * May be NULL, if no additional conflicts are needed.
    * Zero-terminated array of pointers to char arrays.
    */
   const char **conflicts;

   /**
    * Requires the given plugins. (Hard dependency)
    * The plugin will be loaded after required plugins, if they are available.
    * May be NULL, if no requires are needed.
    * Zero-terminated array of pointers to char arrays.
    */
   const char **requires;

   /**
    * Loads after the given plugins. (Optional dependency)
    * The plugin will be loaded after the optional plugins, regardless if they are available.
    * May be NULL, if no requires are needed.
    * Zero-terminated array of pointers to char arrays.
    */
   const char **after;

   /**
    * Groups the plugin belongs to. Plugins may require or load after group.
    * Group name can be same as plugin name.
    * This will not cause recursion if plugin belonging to the group also requires similar named plugin.
    * This is useful when providing api, which other plugins can extend. (ex. compressor)
    * It makes sure the api and the extensions are loaded on require.
    * May be NULL, if no groups are needed
    * Zero-terminated array of pointers to char arrays.
    */
   const char **groups;
};

/**
 * Plugin handle.
 */
typedef size_t plugin_h;

/**
 * Imports plugin with name.
 * Returns plugin handle, 0 if no such plugin.
 */
plugin_h import_plugin(const char *name);

/**
 * Check if plugin has zero-terminated list of methods.
 * Returns true, if all methods with the signatures exist.
 *
 * If the method exists in plugin, import_method for that method will always succeeed.
 * Thus this method can be used to check all hard depencies to another plugin.
 */
bool has_methods(plugin_h plugin, const struct method_info *methods);

/**
 * Imports a method from another plugin.
 * Returns NULL if signature does not match with loaded plugin version, or method is not found.
 */
void* import_method(plugin_h plugin, const char *name, const char *signature);

/**
 * Helper macro for registering methods.
 *
 * Type literals:
 * XXX: Extendable, however we should have good base that does not change
 *
 * u8  | uint8_t
 * i8  | int8_t
 * u16 | uint16_t
 * i16 | int16_t
 * u32 | uint32_t
 * i32 | int32_t
 * u64 | uint64_t
 * i64 | int64_t
 * up  | uintptr_t
 * ip  | intptr_t
 * sz  | size_t
 * f   | float
 * d   | double
 * c   | char
 * b   | bool
 * *   | pointer
 * []  | array, you can use element count inside as C allows compile time checks for element sizes.
 * v   | void
 * fun | function, see function struct above.
 * h   | wlc handle
 *
 * Written as:
 * <return_type>(<arguments>)|<ABI version>
 *
 * Arguments are separated with colons for readibility.
 *
 * Example:
 * *(c[12],i16,i32)|1
 *
 * Returns pointer, takes char array with size of 12, signed short and signed int.
 * For pointers to POD arrays use [] without number instead of *. eg. char* would be c[].
 * For pointers to POD types use <type>*, eg. size_t* would be sz*.
 *
 * The ABI version must be incremented for each revision of the function where meaning of input data changed.
 * This is most important with pointers used as references to data structures.
 * If the data structure changed, you should raise ABI version.
 *
 * This protects against ABI changes and allows backwards compatibility to some degree.
 *
 * The plugins however most likely will always be compiled with some sort of helper tool and exist in
 * central repository, so ABI breakages should not be common, but still something to think about.
 *
 * Intentional bad plugins can still cause havoc, but these are the tradeoffs for the power of native shared libraries,
 * without using sandboxing and IPC. I think this is not a problem when plugins are available and easily compiled
 * from central repository where bad plugins can be fixed or removed.
 *
 * Of course, it is always good idea to check source of your plugins.
 */
#define REGISTER_METHOD(fun, sig) { .info = { .name = #fun, .signature = sig }, .function = fun, .deprecated = false }

/**
 * Same as above, but marks as deprecated.
 */
#define REGISTER_DEPRECATED(fun, sig) { .info = { .name = #fun, .signature = sig }, .function = fun, .deprecated = true }

/**
 * Helper macro for filling method_info struct, mainly for has_methods function.
 */
#define METHOD(fun, sig) { .name = fun, .signature = sig }

/**
 * Helper macro for filling function struct, use when passing functions to other plugins.
 */
#define FUN(fun, sig) &(struct function){ .function = fun, .signature = sig }

#endif /* __orbment_plugin_h__ */
