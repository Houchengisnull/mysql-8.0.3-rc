#ifndef ITEM_JSON_FUNC_INCLUDED
#define ITEM_JSON_FUNC_INCLUDED

/* Copyright (c) 2015, 2017, Oracle and/or its affiliates. All rights reserved.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 of the License.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA */

#include <stddef.h>
#include <sys/types.h>
#include <utility>              // std::forward

#include "binary_log_types.h"
#include "m_ctype.h"
#include "my_inttypes.h"
#include "my_time.h"
#include "mysql/udf_registration_types.h"
#include "mysql_com.h"
#include "prealloced_array.h"   // Prealloced_array
#include "sql/enum_query_type.h"
#include "sql/field.h"
#include "sql/item.h"
#include "sql/item_func.h"
#include "sql/item_strfunc.h"   // Item_str_func
#include "sql/json_path.h"      // Json_path
#include "sql/mem_root_array.h" // Mem_root_array
#include "sql/my_decimal.h"
#include "sql/parse_tree_node_base.h"
#include "sql_string.h"

class Item_func_like;
class Json_scalar_holder;
class Json_wrapper;
class PT_item_list;
class THD;

/** For use by JSON_CONTAINS_PATH() and JSON_SEARCH() */
enum enum_one_or_all_type
{
  ooa_one,
  ooa_all,
  ooa_null,
  ooa_error,
  ooa_uninitialized
};

/**
  Path cache for JSON functions. Caches parsed path
  objects for arguments which are string literals.
  Maintains a vector of path objects and an array of
  ints which map path argument numbers to slots in
  the array.
*/
class Json_path_cache
{
private:
  /// Holder for path strings.
  String m_path_value;

  /// List of paths.
  Prealloced_array<Json_path, 8> m_paths;

  /// Enum that tells the status of a cell in m_paths.
  enum class enum_path_status : uint8
  { UNINITIALIZED, OK_NOT_NULL, OK_NULL, ERROR };

  /// Struct that points to a cell in m_paths and tells its status.
  struct Path_cell
  {
    enum_path_status m_status= enum_path_status::UNINITIALIZED;
    size_t m_index= 0;
  };

  /// Map argument indexes to indexes into m_paths.
  Mem_root_array<Path_cell> m_arg_idx_to_vector_idx;

public:
  Json_path_cache(THD *thd, uint size);
  ~Json_path_cache();

  /**
    Parse a path expression if necessary. Does nothing if the path
    expression is constant and it has already been parsed. Assumes that
    we've already verified that the path expression is not null. Raises an
    error if the path expression is syntactically incorrect. Raises an
    error if the path expression contains wildcard tokens but is not
    supposed to. Otherwise puts the parsed path onto the
    path vector.

    @param[in]  args             Array of args to a JSON function
    @param[in]  arg_idx          Index of the path_expression in args
    @param[in]  forbid_wildcards True if the path shouldn't contain * or **

    @returns false on success (valid path or NULL), true on error
  */
  bool parse_and_cache_path(Item ** args, uint arg_idx,
                            bool forbid_wildcards);


  /**
    Return an already parsed path expression.

    @param[in]  arg_idx   Index of the path_expression in the JSON function args

    @returns the already parsed path, possibly NULL
  */
  Json_path *get_path(uint arg_idx);

  /**
    Reset the cache for re-use when a statement is re-executed.
  */
  void reset_cache();
};

/* JSON function support  */

/**
  Base class for all item functions that a return JSON value
*/
class Item_json_func : public Item_func
{
  /// Can this function type be used in partial update?
  virtual bool can_use_in_partial_update() const { return false; }
protected:
  /// String used when reading JSON binary values or JSON text values.
  String m_value;
  /// String used for converting JSON text values to utf8mb4 charset.
  String m_conversion_buffer;
  /// String used for converting a JSON value to text in val_str().
  String m_string_buffer;

  // Cache for constant path expressions
  Json_path_cache m_path_cache;

  type_conversion_status save_in_field_inner(Field *field, bool no_conversions)
    override;

  /**
    Target column for partial update, if this function is used in an
    update statement and partial update can be used.
  */
  const Field_json *m_partial_update_column= nullptr;

public:
  /**
    Construct an Item_json_func instance.
    @param thd   THD handle
    @param args  arguments to forward to Item_func's constructor
  */
  template <typename... Args>
  Item_json_func(THD *thd, Args&&... args)
    : Item_func(std::forward<Args>(args)...), m_path_cache(thd, arg_count)
  {
    set_data_type_json();
  }

  bool resolve_type(THD *) override
  {
    maybe_null= true;
    return false;
  }
  enum Item_result result_type() const override { return STRING_RESULT; }
  String *val_str(String *arg) override;
  bool get_date(MYSQL_TIME *ltime, my_time_flags_t fuzzydate) override;
  bool get_time(MYSQL_TIME *ltime) override;
  longlong val_int() override;
  double val_real() override;
  my_decimal *val_decimal(my_decimal *decimal_value) override;

  void cleanup() override;

  Item_result cast_to_int_type() const override { return INT_RESULT; }

  /**
    Does this function call support partial update of the given JSON column?

    JSON_SET, JSON_REPLACE and JSON_REMOVE support partial update of a JSON
    column if the JSON column is the first argument of the function call, or if
    the first argument is a sequence of nested JSON_SET, JSON_REPLACE and
    JSON_REMOVE calls in which the JSON column is the first argument of the
    inner function call.

    For example, this expression can be used to partially update column
    `json_col`:

        JSON_SET(JSON_REPLACE(json_col, path1, val1), path2, val2)
  */
  bool supports_partial_update(const Field_json *field) const override;

  /**
    Mark this expression as used in partial update. Should only be
    called if #supports_partial_update returns true.
  */
  void mark_for_partial_update(const Field_json *field);
};

/**
  Return the JSON value of the argument in a wrapper. Abstracts whether
  the value comes from a field or a function. Does not handle literals.
  See also get_json_wrapper.

  @param[in]     args     the arguments
  @param[in]     arg_idx  the argument index
  @param[in,out] result   the JSON value wrapper

  @returns false iff the argument is a JSON field or function result
*/
bool json_value(Item **args, uint arg_idx, Json_wrapper *result);

/**
  Return the JSON value of the argument in a wrapper. Abstracts whether
  the value comes from a field or a function or a valid JSON text.

  @param[in]  args          the arguments
  @param[in]  arg_idx       the argument index
  @param[out] str           the string buffer
  @param[in]  func_name     the name of the function we are executing
  @param[out] wrapper       the JSON value wrapper
  @param[in]  handle_numbers_as_double
                            whether numbers should be handled as double. If set
                            to TRUE, all numbers are parsed as DOUBLE
  @returns false if we found a value or NULL, true if not.
*/
bool get_json_wrapper(Item **args, uint arg_idx, String *str,
                      const char *func_name, Json_wrapper *wrapper,
                      bool handle_numbers_as_double= false);

/**
  Convert Json values or MySQL values to JSON.

  @param[in]     args       arguments to function
  @param[in]     arg_idx    the index of the argument to process
  @param[in]     calling_function    name of the calling function
  @param[in,out] value      working area (if the returned Json_wrapper points
                            to a binary value rather than a DOM, this string
                            will end up holding the binary representation, and
                            it must stay alive until the wrapper is destroyed
                            or converted from binary to DOM)
  @param[in,out] tmp        temporary scratch space for converting strings to
                            the correct charset; only used if accept_string is
                            true and conversion is needed
  @param[in,out] wr         the result wrapper
  @param[in,out] scalar     pointer to pre-allocated memory that can be
                            borrowed by the result wrapper if the result is a
                            scalar. If the pointer is NULL, memory for a
                            scalar result will be allocated on the heap.
  @param[in]                accept_string
                            if true, accept MySQL strings as JSON strings
                            by converting them to UTF8, else emit an error
  @returns false if we found a value or NULL, true otherwise
*/
bool get_json_atom_wrapper(Item **args, uint arg_idx,
                           const char *calling_function, String *value,
                           String *tmp, Json_wrapper *wr,
                           Json_scalar_holder *scalar, bool accept_string);

/**
  Check a non-empty val for character set. If it has character set
  my_charset_binary, signal error and return false. Else, try to convert to
  my_charset_utf8mb4_binary. If this fails, signal error and return true, else
  return false.

  @param[in]     val       the string to be checked
  @param[in,out] buf       buffer to hold the converted string
  @param[out]    resptr    the resulting, possibly converted string,
                           only set if no error
  @param[out]    reslength the length of resptr
  @param[in]     require_string
                           If true, give error messages if binary string. If we
                           see a conversion error (space), we give error
                           notwithstanding this parameter value

  @returns True if the string could not be converted. False on success.
*/
bool ensure_utf8mb4(String *val,
                    String *buf,
                    const char **resptr,
                    size_t *reslength,
                    bool require_string);

/**
  Represents the JSON function JSON_VALID( <value> )
*/
class Item_func_json_valid final : public Item_int_func
{
  String m_value;
public:
  Item_func_json_valid(const POS &pos, Item *a) : Item_int_func(pos, a)
  {}

  const char *func_name() const override { return "json_valid"; }

  bool is_bool_func() const override { return true; }

  longlong val_int() override;

  bool resolve_type(THD *) override
  {
    maybe_null= true;
    return false;
  }
};

/**
  Represents the JSON function JSON_CONTAINS()
*/
class Item_func_json_contains final : public Item_int_func
{
  String m_doc_value;
  Json_path_cache m_path_cache;

 public:
  Item_func_json_contains(THD *thd, const POS &pos, PT_item_list *a)
    : Item_int_func(pos, a), m_path_cache(thd, arg_count)
  {}

  const char *func_name() const override { return "json_contains"; }

  bool is_bool_func() const override { return true; }

  longlong val_int() override;

  bool resolve_type(THD *) override
  {
    maybe_null= true;
    return false;
  }

  /** Cleanup between executions of the statement */
  void cleanup() override;
};

/**
  Represents the JSON function JSON_CONTAINS_PATH()
*/
class Item_func_json_contains_path final : public Item_int_func
{
  String m_doc_value;
  String m_one_or_all_value;
  enum_one_or_all_type m_cached_ooa;

  // Cache for constant path expressions
  Json_path_cache m_path_cache;

public:
  Item_func_json_contains_path(THD *thd, const POS &pos, PT_item_list *a)
    : Item_int_func(pos, a),
    m_cached_ooa(ooa_uninitialized), m_path_cache(thd, arg_count)
  {}

  const char *func_name() const override { return "json_contains_path"; }

  bool is_bool_func() const override { return true; }

  longlong val_int() override;

  bool resolve_type(THD *) override
  {
    maybe_null= true;
    return false;
  }

  /** Cleanup between executions of the statement */
  void cleanup() override;
};

/**
  Represents the JSON function JSON_TYPE
*/
class Item_func_json_type :public Item_str_func
{
  String m_value;
public:
  Item_func_json_type(const POS &pos, Item *a) : Item_str_func(pos, a)
  {}

  const char *func_name() const override { return "json_type"; }

  bool resolve_type(THD *) override;

  String *val_str(String *) override;
};

/**
  Represents a "CAST( <value> AS JSON )" coercion.
*/
class Item_json_typecast final : public Item_json_func
{
public:
  Item_json_typecast(THD *thd, const POS &pos, Item *a) : Item_json_func(thd, pos, a)
  {}

  void print(String *str, enum_query_type query_type) override;
  const char *func_name() const override { return "cast_as_json"; }
  const char *cast_type() const { return "json"; }
  bool val_json(Json_wrapper *wr) override;
};

/**
  Represents the JSON function JSON_LENGTH()
*/
class Item_func_json_length final : public Item_int_func
{
  String m_doc_value;

  // Cache for constant path expressions
  Json_path_cache m_path_cache;

public:
  Item_func_json_length(THD *thd, const POS &pos, Item *a)
    : Item_int_func(pos, a), m_path_cache(thd, 1)
  {}

  Item_func_json_length(THD *thd, const POS &pos, Item *a, Item *b)
    : Item_int_func(pos, a, b), m_path_cache(thd, 2)
  {}

  bool resolve_type(THD *) override
  {
    maybe_null= true;
    return false;
  }

  const char *func_name() const override { return "json_length"; }

  longlong val_int() override;

  void cleanup() override;
};

/**
  Represents the JSON function JSON_DEPTH()
*/
class Item_func_json_depth final : public Item_int_func
{
  String m_doc_value;

public:
  Item_func_json_depth(const POS &pos, Item *a)
    : Item_int_func(pos, a)
  {}

  const char *func_name() const override { return "json_depth"; }

  longlong val_int() override;
};

/**
  Represents the JSON function JSON_KEYS()
*/
class Item_func_json_keys :public Item_json_func
{
  String m_doc_value;

public:
  Item_func_json_keys(THD *thd, const POS &pos, Item *a)
    : Item_json_func(thd, pos, a)
  {}

  Item_func_json_keys(THD *thd, const POS &pos, Item *a, Item *b)
    : Item_json_func(thd, pos, a, b)
  {}

  const char *func_name() const override { return "json_keys"; }

  bool val_json(Json_wrapper *wr) override;
};

/**
  Represents the JSON function JSON_EXTRACT()
*/
class Item_func_json_extract final : public Item_json_func
{
  String m_doc_value;

public:
  Item_func_json_extract(THD *thd, const POS &pos, PT_item_list *a)
    : Item_json_func(thd, pos, a)
  {}

  Item_func_json_extract(THD *thd, const POS &pos, Item *a, Item *b)
    : Item_json_func(thd, pos, a, b)
  {}

  const char *func_name() const override
  {
    return "json_extract";
  }

  bool val_json(Json_wrapper *wr) override;

  bool eq(const Item *item, bool binary_cmp) const override;
};

/**
  Represents the JSON function JSON_ARRAY_APPEND()
*/
class Item_func_json_array_append :public Item_json_func
{
  String m_doc_value;

public:
  Item_func_json_array_append(THD *thd, const POS &pos, PT_item_list *a)
    : Item_json_func(thd, pos, a)
  {}

  const char *func_name() const override { return "json_array_append"; }

  bool val_json(Json_wrapper *wr) override;
};

/**
  Represents the JSON function JSON_INSERT()
*/
class Item_func_json_insert :public Item_json_func
{
  String m_doc_value;

public:
  Item_func_json_insert(THD *thd, const POS &pos, PT_item_list *a)
    : Item_json_func(thd, pos, a)
  {}

  const char *func_name() const override { return "json_insert"; }

  bool val_json(Json_wrapper *wr) override;
};

/**
  Represents the JSON function JSON_ARRAY_INSERT()
*/
class Item_func_json_array_insert :public Item_json_func
{
  String m_doc_value;

public:
  Item_func_json_array_insert(THD *thd, const POS &pos, PT_item_list *a)
    : Item_json_func(thd, pos, a)
  {}

  const char *func_name() const override { return "json_array_insert"; }

  bool val_json(Json_wrapper *wr) override;
};

/**
  Common base class for JSON_SET() and JSON_REPLACE().
*/
class Item_func_json_set_replace :public Item_json_func
{
  /// True if this is JSON_SET, false if it is JSON_REPLACE.
  const bool m_json_set;
  String m_doc_value;
  Json_path_clone m_path;
  bool can_use_in_partial_update() const override { return true; }

protected:
  template <typename... Args>
  Item_func_json_set_replace(bool json_set, Args&&... args)
    : Item_json_func(std::forward<Args>(args)...), m_json_set(json_set)
  {}

public:
  bool val_json(Json_wrapper *wr) override;
};

/**
  Represents the JSON function JSON_SET()
*/
class Item_func_json_set :public Item_func_json_set_replace
{
public:
  template <typename... Args>
  Item_func_json_set(Args&&... args)
    : Item_func_json_set_replace(true, std::forward<Args>(args)...)
  {}

  const char *func_name() const override { return "json_set"; }
};

/**
  Represents the JSON function JSON_REPLACE()
*/
class Item_func_json_replace :public Item_func_json_set_replace
{
public:
  template <typename... Args>
  Item_func_json_replace(Args&&... args)
    : Item_func_json_set_replace(false, std::forward<Args>(args)...)
  {}

  const char *func_name() const override { return "json_replace"; }
};

/**
  Represents the JSON function JSON_ARRAY()
*/
class Item_func_json_array :public Item_json_func
{
public:
  template <typename... Args>
  Item_func_json_array(Args&&... args)
    : Item_json_func(std::forward<Args>(args)...)
  {}

  const char *func_name() const override { return "json_array"; }

  bool val_json(Json_wrapper *wr) override;
};

/**
  Represents the JSON function JSON_OBJECT()
*/
class Item_func_json_row_object :public Item_json_func
{
  String tmp_key_value;
public:
  Item_func_json_row_object(THD *thd, const POS &pos, PT_item_list *a)
    : Item_json_func(thd, pos, a)
  {}

  const char *func_name() const override { return "json_object"; }

  bool val_json(Json_wrapper *wr) override;
};

/**
  Represents the JSON function JSON_SEARCH()
*/
class Item_func_json_search :public Item_json_func
{
  String m_doc_value;
  String m_one_or_all_value;
  enum_one_or_all_type m_cached_ooa;
  String m_escape;

  // LIKE machinery
  Item_string *m_source_string_item;
  Item_func_like *m_like_node;
public:
  /**
   Construct a JSON_SEARCH() node.

   @param     thd Current session.
   @param[in] pos Parser position
   @param[in] a   Nodes which must be fixed (i.e. bound/resolved)

   @returns a JSON_SEARCH() node.
  */
  Item_func_json_search(THD *thd, const POS &pos, PT_item_list *a)
    : Item_json_func(thd, pos, a),
    m_cached_ooa(ooa_uninitialized)
  {}


  const char *func_name() const override { return "json_search"; }

  bool val_json(Json_wrapper *wr) override;

  /**
    Bind logic for the JSON_SEARCH() node.
  */
  bool fix_fields(THD *, Item **) override;

  void cleanup() override;
};

/**
  Represents the JSON function JSON_REMOVE()
*/
class Item_func_json_remove :public Item_json_func
{
  String m_doc_value;
  bool can_use_in_partial_update() const override { return true; }

public:
  template <typename... Args>
  Item_func_json_remove(Args&&... args)
    : Item_json_func(std::forward<Args>(args)...)
  {}

  const char *func_name() const override { return "json_remove"; }

  bool val_json(Json_wrapper *wr) override;
};

/**
  Represents the JSON function JSON_MERGE_PRESERVE.
*/
class Item_func_json_merge_preserve :public Item_json_func
{
public:
  Item_func_json_merge_preserve(THD *thd, const POS &pos, PT_item_list *a)
    : Item_json_func(thd, pos, a)
  {}

  const char *func_name() const override { return "json_merge_preserve"; }

  bool val_json(Json_wrapper *wr) override;
};

/**
  Represents the JSON function JSON_MERGE. It is a deprecated alias
  for JSON_MERGE_PRESERVE.
*/
class Item_func_json_merge :public Item_func_json_merge_preserve
{
public:
  Item_func_json_merge(THD *thd, const POS &pos, PT_item_list *a);
};


/**
  Represents the JSON function JSON_MERGE_PATCH.
*/
class Item_func_json_merge_patch :public Item_json_func
{
public:
  Item_func_json_merge_patch(THD *thd, const POS &pos, PT_item_list *a)
    : Item_json_func(thd, pos, a)
  {}

  const char *func_name() const override { return "json_merge_patch"; }

  bool val_json(Json_wrapper *wr) override;
};

/**
  Represents the JSON function JSON_QUOTE()
*/
class Item_func_json_quote :public Item_str_func
{
  String m_value;
public:
  Item_func_json_quote(const POS &pos, PT_item_list *a)
    : Item_str_func(pos, a)
  {}

  const char *func_name() const override { return "json_quote"; }

  bool resolve_type(THD *) override
  {
    maybe_null= true;

    /*
     Any interior character could be replaced by a 6 character
     escape sequence. Plus we will add 2 framing quote characters.
    */
    uint32 max_char_length= (6 * args[0]->max_char_length()) + 2;
    set_data_type_string(max_char_length, &my_charset_utf8mb4_bin);
    return false;
  };

  String *val_str(String *tmpspace) override;
};

/**
  Represents the JSON function JSON_UNQUOTE()
*/
class Item_func_json_unquote :public Item_str_func
{
  String m_value;
public:
  Item_func_json_unquote(const POS &pos, PT_item_list *a)
    : Item_str_func(pos, a)
  {}

  Item_func_json_unquote(const POS &pos, Item *a)
    : Item_str_func(pos, a)
  {}

  const char *func_name() const override { return "json_unquote"; }

  bool resolve_type(THD *) override
  {
    maybe_null= true;
    set_data_type_string(args[0]->max_char_length(), &my_charset_utf8mb4_bin);
    return false;
  };

  String *val_str(String *str) override;
};

/**
  Represents the JSON_PRETTY function.
*/
class Item_func_json_pretty final :public Item_str_func
{
public:
  Item_func_json_pretty(const POS &pos, Item *a) : Item_str_func(pos, a)
  {}

  const char *func_name() const override { return "json_pretty"; }

  bool resolve_type(THD*) override
  {
    set_data_type_string(MAX_BLOB_WIDTH, &my_charset_utf8mb4_bin);
    return false;
  }

  String *val_str(String *str) override;
};

/**
  Class that represents the function JSON_STORAGE_SIZE.
*/
class Item_func_json_storage_size final : public Item_int_func
{
public:
  Item_func_json_storage_size(const POS &pos, Item *a)
    : Item_int_func(pos, a)
  {}
  const char *func_name() const override { return "json_storage_size"; }
  longlong val_int() override;
};

/**
  Class that represents the function JSON_STORAGE_FREE.
*/
class Item_func_json_storage_free final : public Item_int_func
{
public:
  Item_func_json_storage_free(const POS &pos, Item *a)
    : Item_int_func(pos, a)
  {}
  const char *func_name() const override { return "json_storage_free"; }
  longlong val_int() override;
};

/**
  Turn a GEOMETRY value into a JSON value per the GeoJSON specification revison 1.0.
  This method is implemented in item_geofunc.cc.

  @param[in,out] wr The wrapper to be stuffed with the JSON value.
  @param[in]     geometry_arg The source GEOMETRY value.
  @param[in]     calling_function Name of user-invoked function (for errors)
  @param[in]     max_decimal_digits See the user documentation for ST_AsGeoJSON.
  @param[in]     add_bounding_box See the user documentation for ST_AsGeoJSON.
  @param[in]     add_short_crs_urn See the user documentation for ST_AsGeoJSON.
  @param[in]     add_long_crs_urn See the user documentation for ST_AsGeoJSON.
  @param[in,out] geometry_srid Spatial Reference System Identifier to be filled in.

  @return false if the conversion succeeds, true otherwise
*/
bool geometry_to_json(Json_wrapper *wr, Item *geometry_arg,
                      const char *calling_function,
                      int max_decimal_digits,
                      bool add_bounding_box,
                      bool add_short_crs_urn,
                      bool add_long_crs_urn,
                      uint32 *geometry_srid);


/**
  Convert JSON values or MySQL values to JSON. Converts SQL NULL
  to the JSON null literal.

  @param[in]     args       arguments to function
  @param[in]     arg_idx    the index of the argument to process
  @param[in]     calling_function    name of the calling function
  @param[in,out] value      working area (if the returned Json_wrapper points
                            to a binary value rather than a DOM, this string
                            will end up holding the binary representation, and
                            it must stay alive until the wrapper is destroyed
                            or converted from binary to DOM)
  @param[in,out] tmp        temporary scratch space for converting strings to
                            the correct charset; only used if accept_string is
                            true and conversion is needed
  @param[in,out] wr         the result wrapper
  @returns false if we found a value or NULL, true otherwise
*/
bool get_atom_null_as_null(Item **args, uint arg_idx,
                           const char *calling_function, String *value,
                           String *tmp, Json_wrapper *wr);

/**
  Helper method for Item_func_json_* methods. Check whether an argument
  can be converted to a utf8mb4 string.

  @param[in]  arg_item    An argument Item
  @param[out] value       Where to materialize the arg_item's string value
  @param[out] utf8_res    Buffer for use by ensure_utf8mb4.
  @param[out] safep       String pointer after any relevant conversion
  @param[out] safe_length Corresponding string length

  @returns true if the Item is not a utf8mb4 string
*/
bool get_json_string(Item *arg_item,
                     String *value,
                     String *utf8_res,
                     const char **safep,
                     size_t *safe_length);


#endif /* ITEM_JSON_FUNC_INCLUDED */