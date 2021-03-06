/*
   Copyright (c) 2013, 2017, Oracle and/or its affiliates. All rights reserved.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 of the License.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software Foundation,
   51 Franklin Street, Suite 500, Boston, MA 02110-1335 USA */

#include "sql/transaction_info.h"

#include <string.h>

#include "mysqld_error.h"       // ER_*
#include "sql/derror.h"         // ER_THD
#include "sql/mysqld.h"         // global_system_variables
#include "sql/psi_memory_key.h" // key_memory_thd_transactions
#include "sql/sql_class.h"      // THD_STAGE_INFO
#include "sql/sql_error.h"      // Sql_condition
#include "sql/system_variables.h" // System_variables


typedef struct st_changed_table_list
{
  struct        st_changed_table_list *next;
  char	        *key;
  uint32        key_length;
} CHANGED_TABLE_LIST;


Transaction_ctx::Transaction_ctx()
  : m_savepoints(NULL), m_xid_state(),
    last_committed(0), sequence_number(0),
    m_rpl_transaction_ctx(), m_transaction_write_set_ctx()
{
  memset(&m_scope_info, 0, sizeof(m_scope_info));
  memset(&m_flags, 0, sizeof(m_flags));
  init_sql_alloc(key_memory_thd_transactions, &m_mem_root,
                 global_system_variables.trans_alloc_block_size,
                 global_system_variables.trans_prealloc_size);
}


void Transaction_ctx::push_unsafe_rollback_warnings(THD *thd)
{
  if (m_scope_info[SESSION].has_modified_non_trans_table())
    push_warning(thd, Sql_condition::SL_WARNING,
                 ER_WARNING_NOT_COMPLETE_ROLLBACK,
                 ER_THD(thd, ER_WARNING_NOT_COMPLETE_ROLLBACK));

  if (m_scope_info[SESSION].has_created_temp_table())
    push_warning(thd, Sql_condition::SL_WARNING,
                 ER_WARNING_NOT_COMPLETE_ROLLBACK_WITH_CREATED_TEMP_TABLE,
                 ER_THD(thd, ER_WARNING_NOT_COMPLETE_ROLLBACK_WITH_CREATED_TEMP_TABLE));

  if (m_scope_info[SESSION].has_dropped_temp_table())
    push_warning(thd, Sql_condition::SL_WARNING,
                 ER_WARNING_NOT_COMPLETE_ROLLBACK_WITH_DROPPED_TEMP_TABLE,
                 ER_THD(thd, ER_WARNING_NOT_COMPLETE_ROLLBACK_WITH_DROPPED_TEMP_TABLE));
}


void Transaction_ctx::register_ha(
  enum_trx_scope scope, Ha_trx_info *ha_info, handlerton *ht)
{
  ha_info->register_ha(&m_scope_info[scope], ht);
}
