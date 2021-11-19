#include <libgimp/gimp.h>
#include <stdio.h>
#include <string.h>
#include <glib.h>
#include <json-glib/json-glib.h>
#include <json-glib/json-gobject.h>

#define PLUG_IN_PROC_NAME "boardgame-component-generator"
#define PLUG_IN_BINARY "boardgame-component-generator-bin"

static void query (void);

static void run (
    const gchar      *name,
    gint              nparams,
    const GimpParam  *param,
    gint             *nreturn_vals,
    GimpParam       **return_vals
);

GimpPlugInInfo PLUG_IN_INFO = {
  NULL,
  NULL,
  query,
  run
};

MAIN ()

static void query (void)
{
  static GimpParamDef args[] = {
    {
      GIMP_PDB_INT32,
      "run-mode",
      "Run mode"
    },
    {
      GIMP_PDB_IMAGE,
      "image",
      "Input image"
    },
    {
      GIMP_PDB_DRAWABLE,
      "drawable",
      "Input drawable"
    },
    {
      GIMP_PDB_STRING,
      "project_dir",
      "Project directory"
    }
  };

  gimp_install_procedure (
    PLUG_IN_PROC_NAME,
    "Boardgame component generator",
    "Generates boardgame components",
    "Marcin Niesluchowski",
    "Marcin Niesluchowski",
    "2021",
    "Generate boardgame components",
    "RGB*, GRAY*",
    GIMP_PLUGIN,
    G_N_ELEMENTS (args), 0,
    args, NULL);

  gimp_plugin_menu_register (PLUG_IN_PROC_NAME,
                             "<Image>/File/Export");
}

typedef enum {
  LAYER_TYPE_UNKNOWN = 0,
  LAYER_TYPE_IMAGE = 1,
  LAYER_TYPE_TEXT = 2,
  LAYER_TYPE_BOOL = 3
} LayerType;

static const gchar* LAYER_TYPE_STR_UNKNOWN = "unknown";
static const gchar* LAYER_TYPE_STR_IMAGE = "image";
static const gchar* LAYER_TYPE_STR_TEXT = "text";
static const gchar* LAYER_TYPE_STR_BOOL = "bool";

static LayerType layer_type_from_str(const gchar* str) {
  if (0 == g_strcmp0(str, LAYER_TYPE_STR_IMAGE)) return LAYER_TYPE_IMAGE;
  if (0 == g_strcmp0(str, LAYER_TYPE_STR_TEXT)) return LAYER_TYPE_TEXT;
  if (0 == g_strcmp0(str, LAYER_TYPE_STR_BOOL)) return LAYER_TYPE_BOOL;
  return LAYER_TYPE_UNKNOWN;
}

static const gchar* str_from_layer_type(LayerType type) {
  switch (type) {
    case LAYER_TYPE_IMAGE: return LAYER_TYPE_STR_IMAGE;
    case LAYER_TYPE_TEXT: return LAYER_TYPE_STR_TEXT;
    case LAYER_TYPE_BOOL: return LAYER_TYPE_STR_BOOL;
    default: return LAYER_TYPE_STR_UNKNOWN;
  }
}

typedef struct {
  LayerType type;
  union {
    gchar* image_path;
    gchar* text;
  };
} LayerData;

typedef struct {
  GHashTable* layers;
} ComponentTemplate;

ComponentTemplate* new_component_template(GHashTable* layers) {
  ComponentTemplate *ct = malloc (sizeof (ComponentTemplate));
  ct->layers = layers;
  return ct;
}

void del_component_template(ComponentTemplate* ct) {
  if (!ct) return;
  g_hash_table_destroy(ct->layers);
  free(ct);
}

typedef gpointer (*NewElementCallback)(JsonReader *reader, void* user_data);

static GHashTable* new_hashtable_from_json_object(JsonReader *reader, NewElementCallback callback, GDestroyNotify free_func, void* user_data) {
  if (!json_reader_is_object(reader)) return NULL;

  gint elements_len = json_reader_count_members(reader);
  gchar** members_list = json_reader_list_members(reader);
  GHashTable* hash_table = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, free_func);
  for (gchar** m = members_list; *m != NULL; ++m) {
    if (!json_reader_read_member(reader, *m)) return NULL;

    gpointer data = (*callback)(reader, user_data);
    if (!data) {
      g_hash_table_destroy(hash_table);
      g_strfreev(members_list);
      return NULL;
    }
    g_hash_table_insert(hash_table, g_strdup(*m), data);

    json_reader_end_member(reader);
  }
  g_strfreev(members_list);
  return hash_table;
}

static gpointer new_layer_from_json(JsonReader *reader, void* user_data) {
  if (!json_reader_is_value(reader)) {
    return NULL;
  }
  LayerType type = layer_type_from_str(json_reader_get_string_value(reader));
  if (type == LAYER_TYPE_UNKNOWN) {
    return NULL;
  }

  LayerType* layer_type = g_new(LayerType, 1);
  *layer_type = type;
  return layer_type;
}

static gpointer new_xcf_from_json(JsonReader *reader, void* user_data) {
  if (!json_reader_is_object(reader) ||
      !json_reader_read_member(reader, "layers")) {
    return FALSE;
  }
  GHashTable* layers = new_hashtable_from_json_object(reader, &new_layer_from_json, g_free, NULL);
  if (!layers) {
    return FALSE;
  }
  json_reader_end_member(reader);

  return new_component_template(layers);
}

GHashTable* new_xfcs_from_json(JsonReader *reader) {
  return new_hashtable_from_json_object(reader, &new_xcf_from_json, (GDestroyNotify)&del_component_template, NULL);
}

static GHashTable* parse_json_config(const gchar* config_path) {
  JsonParser *parser = json_parser_new ();
  GError *error = NULL;

  json_parser_load_from_file (parser, config_path, &error);
  if (error) {
    printf("Unable to parse %s: %s\n", config_path, error->message);
    g_error_free (error);
    g_object_unref (parser);
    return NULL;
  }

  JsonReader *reader = json_reader_new (json_parser_get_root (parser));
  GHashTable* xcfs = new_xfcs_from_json(reader);
  g_object_unref (reader);
  g_object_unref (parser);

  return xcfs;
}

// TODO: This just prints layers for now
static gboolean generate_components_from_image(gint32 image_ID, ComponentTemplate* ct, gchar* assets_dir, gchar* out_dir) {
  GHashTableIter iter;
  gpointer key, value;
  g_hash_table_iter_init(&iter, ct->layers);
  printf("Layers:\n");
  while (g_hash_table_iter_next(&iter, &key, &value)) {
    gint32 layer_ID = gimp_image_get_layer_by_name(image_ID, key);
    if (layer_ID == -1) {
      printf("Failed to find %s layer in image\n", (gchar*)key);
      return FALSE;
    }
    LayerType layer_type = *((LayerType*)value);
    const gchar* layer_type_str = str_from_layer_type(layer_type);
    if (!((layer_type == LAYER_TYPE_IMAGE && !gimp_item_is_text_layer(layer_ID))
        || (layer_type == LAYER_TYPE_TEXT && gimp_item_is_text_layer(layer_ID))
        || layer_type == LAYER_TYPE_BOOL)) {
      printf("Layer %s type missmatch\n", (gchar*)key);
      printf("  Config: %s\n", layer_type_str);
      printf("  Image: ");
      if (gimp_item_is_text_layer(layer_ID)) {
        printf("text\n");
      } else {
        printf("image\n");
      }
      return FALSE;
    }

    printf("- %s\n", (gchar*)key);
    printf("  type: %s\n", layer_type_str);
    printf("  size: [%d, %d]\n", gimp_drawable_width(layer_ID), gimp_drawable_height(layer_ID));
  }

  return TRUE;
}

static gboolean save_out_image(gint32 image_ID, gchar* out_file) {
  gboolean ret;

  if (!gimp_file_save(
      GIMP_RUN_NONINTERACTIVE,
      image_ID,
      gimp_image_get_active_drawable(image_ID),
      out_file,
      out_file)) {
    printf("Failed to save image to %s\n", out_file);
    ret = FALSE;
  } else {
    ret = TRUE;
  }

  return ret;
}

static gboolean generate_from_xfc(gchar* xcfs_dir, gchar* assets_dir, gchar* out_dir, gchar* name, ComponentTemplate* ct) {
  gchar* xfc_filename = g_strconcat(name, ".xcf", NULL);
  gchar* xfc_path = g_build_filename(xcfs_dir, xfc_filename, NULL);
  gboolean ret;

  gint32 image_ID = gimp_file_load(GIMP_RUN_NONINTERACTIVE, xfc_path, xfc_path);
  if (image_ID == -1) {
    printf("Input file %s not found\n", xfc_path);
    ret = FALSE;
  } else {
    ret = generate_components_from_image(image_ID, ct, assets_dir, out_dir);
    if (!ret) printf("Failed to generate components from %s image\n", xfc_path);
  }

  gimp_image_delete(image_ID);
  g_free(xfc_path);
  g_free(xfc_filename);

  return ret;
}

static gboolean generate_from_project(gchar* project_dir) {
  gchar* config_path = g_build_filename(project_dir, "config.json", NULL);
  gchar* xcfs_dir = g_build_filename(project_dir, "xcfs", NULL);
  gchar* assets_dir = g_build_filename(project_dir, "assets", NULL);
  gchar* out_dir = g_build_filename(project_dir, "out", NULL);
  gboolean ret = TRUE;

  GHashTable* xcfs = parse_json_config(config_path);
  if (!xcfs) {
    printf("Failed to read %s config\n", config_path);
  } else {
    GHashTableIter iter;
    gpointer key, value;
    g_hash_table_iter_init(&iter, xcfs);
    while (g_hash_table_iter_next(&iter, &key, &value)) {
      ret = generate_from_xfc(xcfs_dir, assets_dir, out_dir, (gchar*)key, (ComponentTemplate*)value);
      if (!ret) break;
    }
    g_hash_table_destroy(xcfs);
  }

  g_free(out_dir);
  g_free(assets_dir);
  g_free(xcfs_dir);
  g_free(config_path);

  return ret;
}

static void run (
    const gchar      *name,
    gint              nparams,
    const GimpParam  *param,
    gint             *nreturn_vals,
    GimpParam       **return_vals
) {
  static GimpParam  values[1];
  GimpRunMode       run_mode;

  /* Setting mandatory output values */
  *nreturn_vals = 1;
  *return_vals  = values;

  values[0].type = GIMP_PDB_STATUS;

  run_mode = param[0].data.d_int32;

  switch (run_mode) {
    case GIMP_RUN_NONINTERACTIVE:
      if (generate_from_project(param[3].data.d_string)) {
        values[0].data.d_status = GIMP_PDB_SUCCESS;
      } else {
        values[0].data.d_status = GIMP_PDB_EXECUTION_ERROR;
      }
      break;
    case GIMP_RUN_INTERACTIVE:
      values[0].data.d_status = GIMP_PDB_CALLING_ERROR;
      g_message("Interactive mode not supported yet!\n");
      break;
    default:
      values[0].data.d_status = GIMP_PDB_CALLING_ERROR;
  }
}