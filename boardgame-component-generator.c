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

static const gchar* const OUT_EXTENSION = "png";

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
    struct text { gchar* value; int vcenter; } text;
  };
} LayerData;

LayerData* new_layer_data_image(gchar* image_path) {
  LayerData* ld = malloc(sizeof(LayerData));
  ld->type = LAYER_TYPE_IMAGE;
  ld->image_path = image_path;
  return ld;
}

LayerData* new_layer_data_text(gchar* value, int vcenter) {
  LayerData* ld = malloc(sizeof(LayerData));
  ld->type = LAYER_TYPE_TEXT;
  ld->text.value = value;
  ld->text.vcenter = vcenter;
  return ld;
}

LayerData* new_layer_data_bool() {
  LayerData* ld = malloc(sizeof(LayerData));
  ld->type = LAYER_TYPE_BOOL;
  return ld;
}

void del_layer_data(LayerData* ld) {
  if (!ld) return;
  switch (ld->type) {
    case LAYER_TYPE_IMAGE:
      g_free(ld->image_path);
      break;
    case LAYER_TYPE_TEXT:
      g_free(ld->text.value);
      break;
  }
  free(ld);
}

typedef struct {
  GHashTable* layers;
  GPtrArray* data;
} ComponentTemplate;

ComponentTemplate* new_component_template(GHashTable* layers, GPtrArray* data) {
  ComponentTemplate *ct = malloc (sizeof (ComponentTemplate));
  ct->layers = layers;
  ct->data = data;
  return ct;
}

void del_component_template(ComponentTemplate* ct) {
  if (!ct) return;
  g_hash_table_destroy(ct->layers);
  g_ptr_array_free(ct->data, TRUE);
  free(ct);
}

typedef gpointer (*NewHashTableElementCallback)(JsonReader *reader, gchar* key, void* user_data);

static GHashTable* new_hashtable_from_json_object(JsonReader *reader, NewHashTableElementCallback callback, GDestroyNotify free_func, void* user_data) {
  if (!json_reader_is_object(reader)) return NULL;

  gint elements_len = json_reader_count_members(reader);
  gchar** members_list = json_reader_list_members(reader);
  GHashTable* hash_table = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, free_func);
  for (gchar** m = members_list; *m != NULL; ++m) {
    if (!json_reader_read_member(reader, *m)) return NULL;

    gpointer data = (*callback)(reader, *m, user_data);
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

typedef gpointer (*NewElementCallback)(JsonReader *reader, void* user_data);

static GPtrArray* new_ptr_array_from_json_array(JsonReader *reader, NewElementCallback callback, GDestroyNotify free_func, void* user_data) {
  if (!json_reader_is_array(reader)) return NULL;
  gint elements_len = json_reader_count_elements(reader);
  GPtrArray* ptr_array = g_ptr_array_sized_new(elements_len);
  g_ptr_array_set_free_func(ptr_array, free_func);
  for (gint i = 0; i < elements_len; ++i) {
    if (!json_reader_read_element(reader, i)) {
      g_ptr_array_free(ptr_array, TRUE);
      return NULL;
    }
    gpointer data = (*callback)(reader, user_data);
    if (!data) {
      g_ptr_array_free(ptr_array, TRUE);
      return NULL;
    }
    g_ptr_array_add(ptr_array, data);
    json_reader_end_element(reader);
  }
  return ptr_array;
}

static gpointer new_layer_from_json(JsonReader *reader, gchar* key, void* user_data) {
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

static gpointer new_layer_data_from_json(JsonReader *reader, gchar* key, void* user_data) {
  gpointer value = g_hash_table_lookup((GHashTable*)user_data, key);
  if (!value) {
    return NULL;
  }

  LayerType layer_type = *((LayerType*)value);
  switch (layer_type) {
    case LAYER_TYPE_IMAGE: {
      if (!json_reader_is_value(reader)) return NULL;
      const gchar* data_value = json_reader_get_string_value(reader);
      return new_layer_data_image(g_strdup(data_value));
    }
    case LAYER_TYPE_TEXT: {
      if (json_reader_is_value(reader)) {
        const gchar* data_value = json_reader_get_string_value(reader);
        return new_layer_data_text(g_strdup(data_value), 0);
      } else if (json_reader_is_object(reader)) {
        gchar* text_value_dup = NULL;
        int center_val = 0;

        if (!json_reader_read_member(reader, "value")) return NULL;
        if (!json_reader_is_value(reader)) { json_reader_end_member(reader); return NULL; }
        const gchar* text_value = json_reader_get_string_value(reader);
        text_value_dup = g_strdup(text_value);
        json_reader_end_member(reader);

        if (json_reader_read_member(reader, "vcenter")) {
          if (json_reader_is_value(reader)) {
            /* accept boolean or int; convert to int */
            center_val = json_reader_get_boolean_value(reader) ? 1 : json_reader_get_int_value(reader);
          }
          json_reader_end_member(reader);
        }

        if (!text_value_dup) return NULL;
        return new_layer_data_text(text_value_dup, center_val);
      }
      return NULL;
    }
    case LAYER_TYPE_BOOL: return new_layer_data_bool();
  }
  return NULL;
}

static gpointer new_data_from_json(JsonReader *reader, void* user_data) {
  return new_hashtable_from_json_object(reader, &new_layer_data_from_json, (GDestroyNotify)&del_layer_data, user_data);
}

static gpointer new_xcf_from_json(JsonReader *reader, gchar* key, void* user_data) {
  if (!json_reader_is_object(reader) ||
      !json_reader_read_member(reader, "layers")) {
    return FALSE;
  }
  GHashTable* layers = new_hashtable_from_json_object(reader, &new_layer_from_json, g_free, NULL);
  if (!layers) {
    return FALSE;
  }
  json_reader_end_member(reader);

  if (!json_reader_read_member(reader, "data")) {
    g_hash_table_destroy(layers);
    return FALSE;
  }

  GPtrArray* data = new_ptr_array_from_json_array(reader, &new_data_from_json, (GDestroyNotify)&g_hash_table_destroy, layers);
  if (!data) {
    g_hash_table_destroy(layers);
    return FALSE;
  }

  json_reader_end_member(reader);

  return new_component_template(layers, data);
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

void print_layer_mismatch(gint32 layer_ID, const gchar* name, LayerType layer_type) {
  printf("Layer %s type missmatch\n", name);
  printf("  Config: %s\n", str_from_layer_type(layer_type));
  printf("  Image: ");
  if (gimp_item_is_text_layer(layer_ID)) {
    printf("text\n");
  } else {
    printf("image\n");
  }
}

gint32 insert_image_layer(gint32 image_ID, gint32 layer_ID, LayerData* layer_data, gchar* assets_dir) {
  gchar* asset_file = g_build_filename(assets_dir, layer_data->image_path, NULL);
  gint32 new_layer_ID = gimp_file_load_layer(GIMP_RUN_NONINTERACTIVE, image_ID, asset_file);
  if (new_layer_ID == -1) {
    printf("Unable to load %s as layer\n", asset_file);
    g_free(asset_file);
    return -1;
  }
  g_free(asset_file);
  gint32 parent_ID = gimp_item_get_parent(layer_ID);
  gint layer_position = gimp_image_get_item_position(image_ID, layer_ID);
  if (!gimp_image_insert_layer(image_ID, new_layer_ID, parent_ID, layer_position)) {
    printf("Unable to add layer to image\n");
    gimp_item_delete(new_layer_ID);
    return -1;
  }
  if (!gimp_layer_scale(new_layer_ID, gimp_drawable_width(layer_ID), gimp_drawable_height(layer_ID), FALSE)) {
    printf("Unable to scale layer\n");
    gimp_image_remove_layer(image_ID, new_layer_ID);
    return -1;
  }
  gint offset_x, offset_y;
  gimp_drawable_offsets(layer_ID, &offset_x, &offset_y);
  if (!gimp_layer_set_offsets(new_layer_ID, offset_x, offset_y)) {
    printf("Unable to set offset of layer\n");
    gimp_image_remove_layer(image_ID, new_layer_ID);
    return -1;
  }
  return new_layer_ID;
}

static gboolean prepare_config_layers(gint32 image_ID, GHashTable* layers) {
  GHashTableIter iter;
  gpointer key, value;
  g_hash_table_iter_init(&iter, layers);
  while (g_hash_table_iter_next(&iter, &key, &value)) {
    gint32 layer_ID = gimp_image_get_layer_by_name(image_ID, key);
    if (layer_ID == -1) {
      printf("Failed to find %s layer in image\n", (gchar*)key);
      return FALSE;
    }
    LayerType layer_type = *((LayerType*)value);
    if (!((layer_type == LAYER_TYPE_IMAGE && !gimp_item_is_text_layer(layer_ID))
        || (layer_type == LAYER_TYPE_TEXT && gimp_item_is_text_layer(layer_ID))
        || layer_type == LAYER_TYPE_BOOL)) {
      print_layer_mismatch(layer_ID, (gchar*)key, layer_type);
      return FALSE;
    }
    gimp_item_set_visible(layer_ID, FALSE);
  }
  return TRUE;
}

static gboolean fit_text_in_bounds(gint32 layer_ID, gint width, gint height, const gchar* text) {
  // Set text
  if (!gimp_text_layer_set_text(layer_ID, text)) {
    gimp_image_delete(new_image_ID);
    printf("Failed to set following text to layer: %s\n", text);
    return FALSE; 
  }

  gdouble font_size = gimp_text_layer_get_font_size(layer_ID);
  GimpUnit font_unit = gimp_text_layer_get_font_size_unit(layer_ID);
  gint text_h = gimp_drawable_height(layer_ID);

  while (text_h > height) {
    // Reduce font size
    font_size -= 1.0;
    if (font_size < 1.0) {
      // No lower font size possible
      printf("Text does not fit in bounding box and cannot reduce font size further: %s\n", text);
      return FALSE;
    }
    gimp_text_layer_set_font_size(layer_ID, font_size, font_unit);
    // Re-set text to trigger re-layout
    gimp_text_layer_set_text(layer_ID, text);
    // Recalculate text size
    text_h = gimp_drawable_height(layer_ID);
  }

  return TRUE;
}

static gboolean generate_component(int i, gint32 image_ID, GHashTable* component_layers, gchar* assets_dir, gchar* out_dir) {
  GHashTableIter iter;
  gpointer key, value;
  gint32 new_image_ID = gimp_image_duplicate(image_ID);
  g_hash_table_iter_init(&iter, component_layers);
  while (g_hash_table_iter_next(&iter, &key, &value)) {
    gchar* layer_name = (gchar*)key;
    LayerData* layer_data = (LayerData*)value;
    gint32 layer_ID = gimp_image_get_layer_by_name(new_image_ID, key);
    gint32 new_layer_ID;
    switch (layer_data->type) {
      case LAYER_TYPE_IMAGE:
        new_layer_ID = insert_image_layer(new_image_ID, layer_ID, layer_data, assets_dir);
        if (new_layer_ID == -1) {
          gimp_image_delete(new_image_ID);
          return FALSE;
        }
        break;
      case LAYER_TYPE_TEXT: {
        const gint width = gimp_drawable_width(layer_ID);
        const gint height = gimp_drawable_height(layer_ID);

        if (!layer_data->text.vcenter) {
          // No "vcenter" param, do as before
          // Fit text within bounds by reducing font size if needed
          if (!fit_text_in_bounds(layer_ID, width, height, layer_data->text.value)) {
            gimp_image_delete(new_image_ID);
            return FALSE;
          }
        } else {
          // "vcenter" param present: make text box dynamic and center it vertically
          gint x, y;
          gimp_drawable_offsets(layer_ID, &x, &y);

          // Fit text within bounds by reducing font size if needed
          if (!fit_text_in_bounds(layer_ID, width, height, layer_data->text.value)) {
            gimp_image_delete(new_image_ID);
            return FALSE;
          }

          // Make text box dynamic (relative)
          gimp_text_layer_set_box_mode(layer_ID, GIMP_TEXT_BOX_DYNAMIC);

          // Ensure max width
          gint text_h = gimp_drawable_height(layer_ID);
          if (text_h > height) {
            text_h = height
          }

          gimp_text_layer_resize(layer_ID, width, text_h);

          // Center text within the original box
          gint new_y = y + (height - text_h) / 2;
          gimp_layer_set_offsets(layer_ID, x, new_y);
        }
        new_layer_ID = layer_ID;
        break;
      }
      case LAYER_TYPE_BOOL:
        new_layer_ID = layer_ID;
        break;
      default:
        gimp_image_delete(new_image_ID);
        printf("Invalid layer type. Something went wrong\n");
        return FALSE;
    }
    gimp_item_set_visible(new_layer_ID, TRUE);
  }

  gint32 final_layer = gimp_image_flatten(new_image_ID);
  gchar* filename = g_strdup_printf("%d.%s", i, OUT_EXTENSION);
  gchar* out_file = g_build_filename(out_dir, filename, NULL);
  gboolean ret = gimp_file_save(
      GIMP_RUN_NONINTERACTIVE,
      new_image_ID,
      final_layer,
      out_file,
      filename);
  if (!ret) {
    printf("Failed to save image to %s\n", out_file);
  }
  g_free(filename);
  g_free(out_file);
  gimp_image_delete(new_image_ID);
  return ret;
}

static gboolean generate_components(gint32 image_ID, GPtrArray* components_layers, gchar* assets_dir, gchar* out_dir) {
  int i;
  for (i = 0; i < components_layers->len; ++i) {
    GHashTable *component_layers = (GHashTable*)(g_ptr_array_index(components_layers, i));
    if (!generate_component(i, image_ID, component_layers, assets_dir, out_dir)) {
      return FALSE;
    }
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

static gchar* create_components_out_dir(gchar* out_dir, gchar* name) {
  gchar* components_out_dir = g_build_filename(out_dir, name, NULL);
  GFile* components_out_dir_gfile = g_file_new_for_path(components_out_dir);
  GError *error = NULL;

  g_file_make_directory_with_parents(components_out_dir_gfile, NULL, &error);
  if (error) {
    if (!g_error_matches(error, G_IO_ERROR, G_IO_ERROR_EXISTS)) {
      printf("Unable to make direcotry %s: %s\n", components_out_dir, error->message);
      g_error_free(error);
      g_object_unref(components_out_dir_gfile);
      g_free(components_out_dir);
      return NULL;
    }
    g_error_free(error);
  }
  g_object_unref(components_out_dir_gfile);
  return components_out_dir;
}

static gboolean generate_from_xfc(gchar* xcfs_dir, gchar* assets_dir, gchar* out_dir, gchar* name, ComponentTemplate* ct) {
  gchar* xfc_filename = g_strconcat(name, ".xcf", NULL);
  gchar* xfc_path = g_build_filename(xcfs_dir, xfc_filename, NULL);

  gint32 image_ID = gimp_file_load(GIMP_RUN_NONINTERACTIVE, xfc_path, xfc_path);
  if (image_ID == -1) {
    printf("Input file %s not found\n", xfc_path);
    g_free(xfc_path);
    g_free(xfc_filename);
    return FALSE;
  }

  if (!prepare_config_layers(image_ID, ct->layers)) {
    gimp_image_delete(image_ID);
    g_free(xfc_path);
    g_free(xfc_filename);
    return FALSE;
  }

  gchar* components_out_dir = create_components_out_dir(out_dir, name);
  if (!components_out_dir) {
    gimp_image_delete(image_ID);
    g_free(xfc_path);
    g_free(xfc_filename);
    return FALSE;
  }

  gboolean ret = generate_components(image_ID, ct->data, assets_dir, components_out_dir);

  g_free(components_out_dir);
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