#include <libgimp/gimp.h>
#include <stdio.h>
#include <string.h>
#include <glib.h>
#include <json-glib/json-glib.h>
#include <json-glib/json-gobject.h>
#include <math.h>
#include <pango/pango.h>
#include <pango/pangocairo.h>
#include <cairo.h>

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
  int vcenter;
  gdouble rotate;
} LayerConfig;

typedef struct {
  LayerConfig* config;
  gchar* value;
} LayerData;

LayerConfig* new_layer_config(LayerType type, int vcenter, gdouble rotate) {
  LayerConfig* lc = malloc(sizeof(LayerConfig));
  lc->type = type;
  lc->vcenter = vcenter;
  lc->rotate = rotate;
  return lc;
}

void del_layer_config(LayerConfig* lc) {
  if (lc) free(lc);
}

LayerData* new_layer_data(LayerConfig* config, gchar* value) {
  LayerData* ld = malloc(sizeof(LayerData));
  ld->config = config;
  ld->value = value;
  return ld;
}

void del_layer_data(LayerData* ld) {
  if (!ld) return;
  g_free(ld->value);
  free(ld);
}

typedef struct {
  GHashTable* layers;
  GPtrArray* data;
  gchar* out_key;
} ComponentTemplate;

ComponentTemplate* new_component_template(GHashTable* layers, GPtrArray* data, gchar* out_key) {
  ComponentTemplate *ct = malloc (sizeof (ComponentTemplate));
  ct->layers = layers;
  ct->data = data;
  ct->out_key = out_key;
  return ct;
}

void del_component_template(ComponentTemplate* ct) {
  if (!ct) return;
  g_hash_table_destroy(ct->layers);
  g_ptr_array_free(ct->data, TRUE);
  if (ct->out_key) g_free(ct->out_key);
  free(ct);
}

typedef gpointer (*NewHashTableElementCallback)(JsonReader *reader, gchar* key, void* user_data);

static GHashTable* new_hashtable_from_json_object(JsonReader *reader, NewHashTableElementCallback callback, GDestroyNotify free_func, void* user_data) {
  if (!json_reader_is_object(reader)) return NULL;

  gchar** members_list = json_reader_list_members(reader);
  GHashTable* hash_table = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, free_func);
  for (gchar** m = members_list; *m != NULL; ++m) {
    if (!json_reader_read_member(reader, *m)) {
      json_reader_end_member(reader);
      printf("%s is not a member", *m);
      return NULL;
    }

    gpointer data = (*callback)(reader, *m, user_data);
    if (!data) {
      printf("Failed to get data from %s\n", *m);
      json_reader_end_member(reader);
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
  LayerType type = LAYER_TYPE_UNKNOWN;
  int vcenter = 0;
  gdouble rotate = 0.0;

  if (json_reader_is_value(reader)) {
    // Simple string format: "layer_name": "text"
    type = layer_type_from_str(json_reader_get_string_value(reader));
    if (type == LAYER_TYPE_UNKNOWN) {
      printf("Layer type unknown: %s\n", json_reader_get_string_value(reader));
      return NULL;
    }
  } else if (json_reader_is_object(reader)) {
    // Object format: "layer_name": {"value": "text", "vcenter": 1, "rotate": 90}
    if (json_reader_read_member(reader, "value")) {
      if (!json_reader_is_value(reader)) {
        printf("%s value is not a value\n", key);
        json_reader_end_member(reader);
        return NULL;
      }
      type = layer_type_from_str(json_reader_get_string_value(reader));
      if (type == LAYER_TYPE_UNKNOWN) {
        printf("Layer type unknown: %s\n", json_reader_get_string_value(reader));
        json_reader_end_member(reader);
        return NULL;
      }
    } else {
      printf("value not a member of %s\n", key);
      json_reader_end_member(reader);
      return NULL;
    }
    json_reader_end_member(reader);

    // Read vcenter if present
    if (json_reader_read_member(reader, "vcenter")) {
      if (json_reader_is_value(reader)) {
        vcenter = json_reader_get_boolean_value(reader) ? 1 : json_reader_get_int_value(reader);
      }
    }
    json_reader_end_member(reader);

    // Read rotate if present  
    if (json_reader_read_member(reader, "rotate")) {
      if (json_reader_is_value(reader)) {
        rotate = json_reader_get_double_value(reader);
      }
    }
    json_reader_end_member(reader);
  } else {
    printf("Layer definition is neither a value nor an object for key %s\n", key);
    return NULL;
  }

  return new_layer_config(type, vcenter, rotate);
}

static gpointer new_layer_data_from_json(JsonReader *reader, gchar* key, void* user_data) {
  LayerConfig* layer_config = (LayerConfig*)g_hash_table_lookup((GHashTable*)user_data, key);
  if (!layer_config) {
    printf("Layer config not found for key %s\n", key);
    return NULL;
  }

  // Use layer configuration for vcenter and rotate
  int vcenter = layer_config->vcenter;
  gdouble rotate = layer_config->rotate;
  LayerType layer_type = layer_config->type;

  switch (layer_type) {
    case LAYER_TYPE_IMAGE:
    case LAYER_TYPE_TEXT: {
      if (!json_reader_is_value(reader)) {
        printf("Layer data is not a value for key %s\n", key);
        return NULL;
      }
      const gchar* data_value = json_reader_get_string_value(reader);
      return new_layer_data(layer_config, g_strdup(data_value));
    }
    case LAYER_TYPE_BOOL: 
      return new_layer_data(layer_config, NULL);
    default:
      printf("Invalid layer type for key %s\n", key);
      return NULL;
  }
}

static gpointer new_data_from_json(JsonReader *reader, void* user_data) {
  return new_hashtable_from_json_object(reader, &new_layer_data_from_json, (GDestroyNotify)&del_layer_data, user_data);
}

static gpointer new_xcf_from_json(JsonReader *reader, gchar* key, void* user_data) {
  if (!json_reader_is_object(reader)) {
    printf("Not an object under key %s\n", key);
    return NULL;
  }

  gchar* out_key = NULL;
  if (json_reader_read_member(reader, "out")) {
    if (json_reader_is_value(reader)) {
      out_key = g_strdup(json_reader_get_string_value(reader));
    }
  }
  json_reader_end_member(reader);

  if (!json_reader_read_member(reader, "layers")) {
    printf("layers not a member of %s\n", key);
    return NULL;
  }
  GHashTable* layers = new_hashtable_from_json_object(reader, &new_layer_from_json, (GDestroyNotify)&del_layer_config, NULL);
  if (!layers) {
    printf("Failed to read layers from %s object\n", key);
    if (out_key) g_free(out_key);
    return NULL;
  }
  json_reader_end_member(reader);

  if (!json_reader_read_member(reader, "data")) {
    printf("data not a member of %s\n", key);
    g_hash_table_destroy(layers);
    if (out_key) g_free(out_key);
    return NULL;
  }

  GPtrArray* data = new_ptr_array_from_json_array(reader, &new_data_from_json, (GDestroyNotify)&g_hash_table_destroy, layers);
  if (!data) {
    printf("Failed to read data from %s object\n", key);
    g_hash_table_destroy(layers);
    if (out_key) g_free(out_key);
    return NULL;
  }

  json_reader_end_member(reader);

  return new_component_template(layers, data, out_key);
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
  gchar* asset_file = g_build_filename(assets_dir, layer_data->value, NULL);
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
    LayerConfig* layer_config = (LayerConfig*)value;
    LayerType layer_type = layer_config->type;
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
    printf("Failed to set following text to layer: %s\n", text);
    return FALSE; 
  }

  if (strlen(text) == 0) {
    return TRUE;
  }

  GimpUnit font_unit;
  gdouble font_size = gimp_text_layer_get_font_size(layer_ID, &font_unit);
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

typedef struct {
  gchar* layer_name;
  gint32 duplicate_layer_id;
  gint position_in_text;
} ImageKeyword;

static GPtrArray* find_image_keywords(const gchar* text, gint32 image_ID) {
  GPtrArray* keywords = g_ptr_array_new_with_free_func(g_free);
  const gchar* current = text;
  gint position = 0;
  while (*current) {
    if (*current == '<' && *(current + 1) == '<') {
      const gchar* start = current + 2;
      const gchar* end = strstr(start, ">>");
      if (end && end > start) {
        gchar* layer_name = g_strndup(start, end - start);
        gint32 found_layer = gimp_image_get_layer_by_name(image_ID, layer_name);
        if (found_layer != -1 && !gimp_item_is_text_layer(found_layer)) {
          ImageKeyword* keyword = g_malloc(sizeof(ImageKeyword));
          keyword->layer_name = layer_name;
          keyword->duplicate_layer_id = -1;
          keyword->position_in_text = position;
          g_ptr_array_add(keywords, keyword);
        } else {
          g_free(layer_name);
        }
        current = end + 2;
        position = (end + 2) - text;
      } else {
        current++;
        position++;
      }
    } else {
      current++;
      position++;
    }
  }
  return keywords;
}

static gchar* replace_keywords_with_spaces(const gchar* text, GPtrArray* keywords) {
  GString* result = g_string_new("");
  const gchar* current = text;
  gint last_pos = 0;
  for (guint i = 0; i < keywords->len; i++) {
    ImageKeyword* keyword = g_ptr_array_index(keywords, i);
    // Find the keyword position in text
    gchar* keyword_pattern = g_strdup_printf("<<%s>>", keyword->layer_name);
    const gchar* found = strstr(current + last_pos, keyword_pattern);
    if (found) {
      // Append text before keyword
      g_string_append_len(result, current + last_pos, found - (current + last_pos));
      // Update the position in processed text (where the spaces will be)
      keyword->position_in_text = result->len;
      // Append two spaces instead of keyword
      g_string_append(result, "  ");
      // Move past the keyword
      last_pos = (found + strlen(keyword_pattern)) - current;
    }
    g_free(keyword_pattern);
  }
  // Append remaining text
  g_string_append(result, current + last_pos);
  return g_string_free(result, FALSE);
}

static gboolean fit_text_in_layer(gint32 layer_ID, const gchar* text, int vcenter) {
  if (!gimp_item_is_text_layer(layer_ID)) {
    printf("Layer is not a text layer\n");
    return FALSE;
  }

  if (strlen(text) == 0) {
    return TRUE;
  }

  // Get original text layer properties
  gint32 original_image_ID = gimp_item_get_image(layer_ID);
  gint text_width = gimp_drawable_width(layer_ID);
  gint text_height = gimp_drawable_height(layer_ID);
  
  // Find and process image keywords
  GPtrArray* keywords = find_image_keywords(text, original_image_ID);
  gchar* processed_text = replace_keywords_with_spaces(text, keywords);
  
  // Create duplicates of image layers for each keyword
  for (guint i = 0; i < keywords->len; i++) {
    ImageKeyword* keyword = g_ptr_array_index(keywords, i);
    gint32 source_layer = gimp_image_get_layer_by_name(original_image_ID, keyword->layer_name);
    
    if (source_layer != -1) {
      keyword->duplicate_layer_id = gimp_layer_copy(source_layer);
      gimp_image_insert_layer(original_image_ID, keyword->duplicate_layer_id, 
                             gimp_item_get_parent(layer_ID), 0);
      gimp_item_set_visible(keyword->duplicate_layer_id, TRUE);
    }
  }
  
  GimpUnit font_unit;
  gdouble font_size = gimp_text_layer_get_font_size(layer_ID, &font_unit);
  gchar* font_name = gimp_text_layer_get_font(layer_ID);
  GimpRGB text_color;
  gimp_text_layer_get_color(layer_ID, &text_color);
  
  // Create new image with text layer width and twice the height
  gint32 temp_image_ID = gimp_image_new(text_width, text_height * 2, GIMP_RGB);
  
  // Create text layer that takes whole image size
  gint32 temp_text_layer_ID = gimp_text_layer_new(temp_image_ID, processed_text, font_name, font_size, font_unit);
  if (temp_text_layer_ID == -1) {
    printf("Failed to create temporary text layer\n");
    gimp_image_delete(temp_image_ID);
    g_free(font_name);
    g_free(processed_text);
    g_ptr_array_free(keywords, TRUE);
    return FALSE;
  }
  
  gimp_image_insert_layer(temp_image_ID, temp_text_layer_ID, -1, 0);
  gimp_text_layer_set_color(temp_text_layer_ID, &text_color);
  
  // Resize text layer to fill the whole image
  gimp_text_layer_resize(temp_text_layer_ID, text_width, text_height * 2);
  
  gdouble current_font_size = font_size;
  gboolean text_fits = FALSE;

  gint y1, y2;

  // Check if visible text exceeds half image size and adjust font size
  while (!text_fits && current_font_size >= 1.0) {
    // Set current font size
    gimp_text_layer_set_font_size(temp_text_layer_ID, current_font_size, font_unit);
    gimp_text_layer_set_text(temp_text_layer_ID, processed_text);
    
    // Create alpha selection to find text bounds
    gimp_image_select_item(temp_image_ID, GIMP_CHANNEL_OP_REPLACE, temp_text_layer_ID);
    
    // Check if selection exists (text is visible)
    gint x1, x2;
    gboolean has_selection;
    gimp_selection_bounds(temp_image_ID, &has_selection, &x1, &y1, &x2, &y2);

    if (!has_selection) {
      // No visible text, font size might be too small or text is empty
      text_fits = TRUE;
    } else {
      // Check if the lowest point of selection (y2) exceeds half image height
      gint half_height = text_height; // Since image height is 2 * text_height, half is text_height
      if (y2 <= half_height) {
        text_fits = TRUE;
      } else {
        // Text exceeds half height, reduce font size
        current_font_size -= 1.0;
      }
    }
    
    // Clear selection
    gimp_selection_none(temp_image_ID);
  }

  // Remove temporary image
  gimp_image_delete(temp_image_ID);
  g_free(font_name);
  
  if (current_font_size < 1.0) {
    printf("Could not fit text within bounds: %s\n", processed_text);
    g_free(processed_text);
    g_ptr_array_free(keywords, TRUE);
    return FALSE;
  }

  // Set the found font size to the original text layer
  gimp_text_layer_set_font_size(layer_ID, current_font_size, font_unit);
  gimp_text_layer_set_text(layer_ID, processed_text);

  if (vcenter) {
    gint x, y;
    gimp_drawable_offsets(layer_ID, &x, &y);
    const gint height_space = (text_height - (y2 - y1)) / 2;
    gint new_y = y - y1 + height_space;
    gimp_layer_set_offsets(layer_ID, x, new_y);
  }
  
  // Position image layers at the locations of the spaces
  if (keywords->len > 0) {
    // Get font name again (the previous one was freed)
    gchar* current_font_name = gimp_text_layer_get_font(layer_ID);
    
    // Get text layer positioning info
    gint text_x, text_y;
    gimp_drawable_offsets(layer_ID, &text_x, &text_y);
    
    // Create a reference text layer with same properties as the main text layer
    gint32 ref_text_layer = gimp_text_layer_new(original_image_ID, processed_text, current_font_name, current_font_size, font_unit);
    gimp_image_insert_layer(original_image_ID, ref_text_layer, -1, 0);
    gimp_text_layer_set_color(ref_text_layer, &text_color);
    
    // Set same position and size as original text layer
    gimp_layer_set_offsets(ref_text_layer, text_x, text_y);
    gimp_text_layer_resize(ref_text_layer, gimp_drawable_width(layer_ID), gimp_drawable_height(layer_ID));
    
    // Text processing and analysis completed
    

    
    // Calculate positions for each keyword by character-by-character measurement
    for (guint i = 0; i < keywords->len; i++) {
      ImageKeyword* keyword = g_ptr_array_index(keywords, i);
      
      if (keyword->duplicate_layer_id != -1) {
        // Create a text layer with just the text up to the keyword position
        gchar* text_up_to_keyword = g_strndup(processed_text, keyword->position_in_text+1); // +1 to include the space
        gint32 measure_layer = gimp_text_layer_new(original_image_ID, text_up_to_keyword, current_font_name, current_font_size, font_unit);
        gimp_image_insert_layer(original_image_ID, measure_layer, -1, 0);
        
        // Set same properties as the reference layer
        gimp_layer_set_offsets(measure_layer, text_x, text_y);
        
        // Create a PangoLayout that matches the GIMP text layer properties
        // and use pango_layout_get_cursor_pos for accurate positioning
        
        // Get text layer properties
        gchar *font_name = gimp_text_layer_get_font(layer_ID);
        if (!font_name) {
            printf("Failed to get font name for positioning\n");
            continue;
        }
        
        GimpUnit unit;
        gdouble font_size = gimp_text_layer_get_font_size(layer_ID, &unit);
        // Convert font size to pixels using the actual unit
        gdouble font_size_pixels = gimp_units_to_pixels(font_size, unit, 72.0); // Assuming 72 DPI
        gdouble line_spacing = gimp_text_layer_get_line_spacing(layer_ID);
        gdouble letter_spacing = gimp_text_layer_get_letter_spacing(layer_ID);
        
        // Create Cairo surface for PangoLayout (needed for proper text measurement)
        cairo_surface_t *surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1, 1);
        cairo_t *cr = cairo_create(surface);
        
        // Create PangoLayout
        PangoLayout *layout = pango_cairo_create_layout(cr);
        
        // Set font description
        PangoFontDescription *font_desc = pango_font_description_from_string(font_name);
        pango_font_description_set_absolute_size(font_desc, font_size_pixels * PANGO_SCALE);
        pango_layout_set_font_description(layout, font_desc);
        
        // Set text content up to the keyword position
        pango_layout_set_text(layout, text_up_to_keyword, -1);
        
        // Set layout properties to match GIMP text layer
        if (line_spacing != 0.0) {
            // Convert line spacing to Pango units and apply
            pango_layout_set_spacing(layout, (int)(line_spacing * PANGO_SCALE));
        }
        
        // Set width constraint if text layer has one
        gint text_layer_width = gimp_drawable_width(layer_ID);
        if (text_layer_width > 0) {
            pango_layout_set_width(layout, text_layer_width * PANGO_SCALE);
            pango_layout_set_wrap(layout, PANGO_WRAP_WORD_CHAR);
        }
        
        // Get cursor position at the end of text_up_to_keyword
        // This gives us the exact position where the keyword replacement should be placed
        gint cursor_index = strlen(text_up_to_keyword);
        PangoRectangle strong_pos, weak_pos;
        pango_layout_get_cursor_pos(layout, cursor_index, &strong_pos, &weak_pos);
        
        // Convert from Pango units to pixels
        gint cursor_x = PANGO_PIXELS(strong_pos.x + strong_pos.width / 2);
        gint cursor_y = PANGO_PIXELS(strong_pos.y + strong_pos.height / 2);
        
        // Calculate final image position
        gint final_image_x = text_x + cursor_x;
        gint final_image_y = text_y + cursor_y;
        
        // Position calculated using accurate Pango cursor positioning
        
        // Cleanup Pango and Cairo objects
        pango_font_description_free(font_desc);
        g_object_unref(layout);
        cairo_destroy(cr);
        cairo_surface_destroy(surface);
        g_free(font_name);
        
        // Resize image to match font size (make it proportional to font size)
        gint image_size = (gint)(current_font_size * 0.9); // 90% of font size for better fit
        gint original_width = gimp_drawable_width(keyword->duplicate_layer_id);
        gint original_height = gimp_drawable_height(keyword->duplicate_layer_id);
        
        // Maintain aspect ratio
        gdouble aspect_ratio = (gdouble)original_width / original_height;
        gint final_width, final_height;
        
        if (aspect_ratio > 1.0) {
          // Wider than tall
          final_width = image_size;
          final_height = (gint)(image_size / aspect_ratio);
        } else {
          // Taller than wide or square
          final_height = image_size;
          final_width = (gint)(image_size * aspect_ratio);
        }
        
        gimp_layer_scale(keyword->duplicate_layer_id, final_width, final_height, FALSE);
        
        // Set final position (center the image at calculated position)
        gint final_x = final_image_x - final_width / 2;
        gint final_y = final_image_y - final_height / 2;
        
        // Position calculated successfully
        
        gimp_layer_set_offsets(keyword->duplicate_layer_id, final_x, final_y);
        
        // Clean up
        gimp_image_remove_layer(original_image_ID, measure_layer);
        g_free(text_up_to_keyword);
      }
    }
    
    // Clean up reference layer
    gimp_image_remove_layer(original_image_ID, ref_text_layer);
    g_free(current_font_name);
  }
  
  // Clean up
  g_free(processed_text);
  g_ptr_array_free(keywords, TRUE);

  return TRUE;
}

static gboolean generate_component(int i, gint32 image_ID, GHashTable* component_layers, gchar* assets_dir, gchar* out_dir, gchar* out_key) {
  GHashTableIter iter;
  gpointer key, value;
  gint32 new_image_ID = gimp_image_duplicate(image_ID);
  g_hash_table_iter_init(&iter, component_layers);
  while (g_hash_table_iter_next(&iter, &key, &value)) {
    gchar* layer_name = (gchar*)key;
    LayerData* layer_data = (LayerData*)value;
    gint32 layer_ID = gimp_image_get_layer_by_name(new_image_ID, key);
    gint32 new_layer_ID;
    printf("Processing layer %s of type %s\n", layer_name, str_from_layer_type(layer_data->config->type));
    switch (layer_data->config->type) {
      case LAYER_TYPE_IMAGE:
        new_layer_ID = insert_image_layer(new_image_ID, layer_ID, layer_data, assets_dir);
        if (new_layer_ID == -1) {
          gimp_image_delete(new_image_ID);
          return FALSE;
        }
        gimp_item_set_visible(new_layer_ID, TRUE);
        break;
      case LAYER_TYPE_TEXT:
        gimp_item_set_visible(layer_ID, TRUE);
        if (!fit_text_in_layer(layer_ID, layer_data->value, layer_data->config->vcenter)) {
            printf("Couldn't fit text in layer: %s\n", layer_data->value);
            gimp_image_delete(new_image_ID);
            return FALSE;
        }
        break;
      case LAYER_TYPE_BOOL:
        gimp_item_set_visible(layer_ID, TRUE);
        break;
      default:
        gimp_image_delete(new_image_ID);
        printf("Invalid layer type. Something went wrong\n");
        return FALSE;
    }

    if (layer_data->config->rotate != 0.0) {
      gdouble angle_rad = layer_data->config->rotate * G_PI / 180.0;
      gimp_item_transform_rotate(layer_ID, angle_rad, TRUE, 0.0, 0.0);
    }
  }

  gint32 final_layer = gimp_image_flatten(new_image_ID);
  gchar* filename = NULL;
  if (out_key) {
    LayerData* out_layer = (LayerData*)g_hash_table_lookup(component_layers, out_key);
    if (out_layer && out_layer->value) {
        filename = g_strdup_printf("%s.%s", out_layer->value, OUT_EXTENSION);
    }
  }
  if (!filename) {
    filename = g_strdup_printf("%d.%s", i, OUT_EXTENSION);
  }
  const size_t to_sanitize_len = strlen(filename)-strlen(OUT_EXTENSION)-1;
  for (char* p = filename; p < filename + to_sanitize_len; ++p) {
    if (!(g_ascii_isalnum(*p) || *p == '-' || *p == '_')) {
      *p = '_';
    }
  }
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

static gboolean generate_components(gint32 image_ID, GPtrArray* components_layers, gchar* assets_dir, gchar* out_dir, gchar* out_key) {
  int i;
  for (i = 0; i < components_layers->len; ++i) {
    GHashTable *component_layers = (GHashTable*)(g_ptr_array_index(components_layers, i));
    if (!generate_component(i, image_ID, component_layers, assets_dir, out_dir, out_key)) {
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

  gboolean ret = generate_components(image_ID, ct->data, assets_dir, components_out_dir, ct->out_key);

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