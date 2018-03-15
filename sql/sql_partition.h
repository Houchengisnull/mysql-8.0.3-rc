#ifndef SQL_PARTITION_INCLUDED
#define SQL_PARTITION_INCLUDED

/* Copyright (c) 2006, 2017, Oracle and/or its affiliates. All rights reserved.

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

#include "lex_string.h"
#include "m_ctype.h"
#include "my_base.h"
#include "my_bitmap.h"
#include "my_inttypes.h"
#include "mysql/udf_registration_types.h"
#include "sql/handler.h"
#include "sql/partition_element.h"   // partition_state
#include "sql/thr_malloc.h"

class Alter_info;
class Alter_table_ctx;
class Create_field;
class Field;
class Item;
class String;
class THD;
class partition_info;
struct TABLE;
struct TABLE_LIST;
struct TABLE_SHARE;

typedef struct charset_info_st CHARSET_INFO;
typedef struct st_bitmap MY_BITMAP;
typedef struct st_ha_create_information HA_CREATE_INFO;
class KEY;

typedef struct st_key_range key_range;
template <class T> class List;

/* Flags for partition handlers */
/*
  Removed HA_CAN_PARTITION (1 << 0) since if handlerton::partition_flags
  is set, then it implies that it have partitioning support.
*/
/*
  Set if the handler can update fields that are part of the partition
  function.
*/
#define HA_CAN_UPDATE_PARTITION_KEY (1 << 1)
/*
  Set if the handler can handle unique indexes where the fields of the
  unique key are not part of the fields of the partition function. Thus
  a unique key can be set on all fields.
*/
#define HA_CAN_PARTITION_UNIQUE (1 << 2)
/* If the engine will use auto partitioning even when not defined. */
#define HA_USE_AUTO_PARTITION (1 << 3)
/**
  The handler can exchange a partition with a non-partitioned table
  of the same handlerton/engine.
*/
#define HA_CAN_EXCHANGE_PARTITION (1 << 4)
/**
  The handler can not use FOREIGN KEYS with partitioning
*/
#define HA_CANNOT_PARTITION_FK (1 << 5)

typedef struct {
  uint32 start_part;
  uint32 end_part;
} part_id_range;

int get_parts_for_update(const uchar *old_data, uchar *new_data,
                         const uchar *rec0, partition_info *part_info,
                         uint32 *old_part_id, uint32 *new_part_id,
                         longlong *func_value);
int get_part_for_delete(const uchar *buf, const uchar *rec0,
                        partition_info *part_info, uint32 *part_id);
void prune_partition_set(const TABLE *table, part_id_range *part_spec);
void set_linear_hash_mask(partition_info *part_info, uint num_parts);
bool fix_partition_func(THD *thd, TABLE *table, bool create_table_ind);
bool partition_key_modified(TABLE *table, const MY_BITMAP *fields);
void get_partition_set(const TABLE *table, uchar *buf, const uint index,
                       const key_range *key_spec,
                       part_id_range *part_spec);
uint get_partition_field_store_length(Field *field);
int get_cs_converted_part_value_from_string(THD *thd,
                                            Item *item,
                                            String *input_str,
                                            String *output_str,
                                            const CHARSET_INFO *cs,
                                            bool use_hex);
void get_full_part_id_from_key(const TABLE *table, uchar *buf,
                               KEY *key_info,
                               const key_range *key_spec,
                               part_id_range *part_spec);
bool mysql_unpack_partition(THD *thd, char *part_buf,
                            uint part_info_len,
                            TABLE *table, bool is_create_table_ind,
                            handlerton *default_db_type,
                            bool *work_part_info_used);
bool make_used_partitions_str(partition_info *part_info,
                              List<const char> *parts);
bool check_part_func_fields(Field **ptr, bool ok_with_charsets);
bool field_is_partition_charset(Field *field);
Item* convert_charset_partition_constant(Item *item, const CHARSET_INFO *cs);
/**
  Append all fields in read_set to string

  @param[in,out] str   String to append to.
  @param[in]     row   Row to append.
  @param[in]     table Table containing read_set and fields for the row.
*/
void append_row_to_str(String &str, const uchar *row, TABLE *table);
void mem_alloc_error(size_t size);
void truncate_partition_filename(MEM_ROOT* root, const char **path);

bool set_part_state(Alter_info *alter_info,
                    partition_info *tab_part_info,
                    enum partition_state part_state,
                    bool include_subpartitions);
void set_all_part_state(partition_info *tab_part_info,
                        enum partition_state part_state);
uint prep_alter_part_table(THD *thd, TABLE *table, Alter_info *alter_info,
                           HA_CREATE_INFO *create_info,
                           Alter_table_ctx *alter_ctx,
                           bool *partition_changed,
                           partition_info **new_part_info);
int expr_to_string(String *val_conv,
                   Item *item_expr,
                   Field *field,
                   const char *field_name,
                   const HA_CREATE_INFO *create_info,
                   List<Create_field> *create_fields);
char *generate_partition_syntax(partition_info *part_info,
                                uint *buf_length, bool use_sql_alloc,
                                bool show_partition_options,
                                bool print_expr,
                                const char *current_comment_start);
bool verify_data_with_partition(TABLE *table, TABLE *part_table,
                                uint32 part_id);
bool compare_partition_options(HA_CREATE_INFO *table_create_info,
                               partition_element *part_elem);

void create_partition_name(char *out, const char *in1,
                           const char *in2, bool translate);
void create_subpartition_name(char *out, const char *in1,
                              const char *in2, const char *in3);

enum enum_partition_keywords
{
  PKW_HASH= 0, PKW_RANGE, PKW_LIST, PKW_KEY, PKW_MAXVALUE, PKW_LINEAR,
  PKW_COLUMNS, PKW_ALGORITHM
};

extern const LEX_STRING partition_keywords[];

#endif /* SQL_PARTITION_INCLUDED */