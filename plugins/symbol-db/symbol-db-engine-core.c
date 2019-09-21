/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 4; tab-width: 4 -*- */
/*
 * anjuta
 * Copyright (C) Massimo Cora' 2007-2009 <maxcvs@email.it>
 * 
 * Some code from IComplete of Martin Stubenschrott <stubenschrott@gmx.net>
 * has been used here.
 *
 * anjuta is free software.
 * 
 * You may redistribute it and/or modify it under the terms of the
 * GNU General Public License, as published by the Free Software
 * Foundation; either version 2 of the License, or (at your option)
 * any later version.
 * 
 * anjuta is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with anjuta.  If not, write to:
 * 	The Free Software Foundation, Inc.,
 * 	51 Franklin Street, Fifth Floor
 * 	Boston, MA  02110-1301, USA.
 */
 
#include <config.h>

#include <time.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <signal.h>
#include <fcntl.h>           /* For O_* constants */
#include <string.h>
#include <regex.h>

#include <gio/gio.h>
#include <libanjuta/interfaces/ianjuta-symbol.h>
#include <libanjuta/anjuta-debug.h>
#include <libanjuta/anjuta-launcher.h>
#include <libanjuta/anjuta-utils.h>
#include <libgda/libgda.h>
#include <sql-parser/gda-sql-parser.h>
#include "readtags.h"
#include "symbol-db-engine-priv.h"
#include "symbol-db-engine-core.h"
#include "symbol-db-engine-utils.h"

#include <glib/gprintf.h>

/*
 * utility macros
 */
#define STATIC_QUERY_POPULATE_INIT_NODE(query_list_ptr, query_type, gda_stmt) { \
	static_query_node *q = g_new0 (static_query_node, 1); \
	q->query_id = query_type; \
	q->query_str = gda_stmt; \
	q->stmt = NULL; \
	q->plist = NULL; \
	query_list_ptr [query_type] = q; \
}

/*
 * typedefs
 */
typedef struct _TableMapTmpHeritage {
	gint symbol_referer_id;
	gchar *field_inherits;
	gchar *field_struct;
	gchar *field_typeref;
	gchar *field_enum;
	gchar *field_union;
	gchar *field_class;
	gchar *field_namespace;

} TableMapTmpHeritage;

typedef struct _TableMapSymbol {	
	gint symbol_id;
	gint file_defined_id;
	gchar *name;
    gint file_position;
	gint is_file_scope;
	gchar *signature;
	gchar *returntype;
	gint scope_definition_id;
	gint scope_id;
	gint type_id;
	gint kind_id;
	gint access_kind_id;
	gint implementation_kind_id;
	gint update_flag;	

} TableMapSymbol;

typedef struct _EngineScanDataAsync {
	GPtrArray *files_list;
	GPtrArray *real_files_list;
	gboolean symbols_update;
	gint scan_id;
	
} EngineScanDataAsync;


typedef void (SymbolDBEngineCallback) (SymbolDBEngine * dbe,
									   gpointer user_data);

/* 
 * signals enum
 */
enum
{
	DB_CONNECTED,
	DB_DISCONNECTED,
	SCAN_BEGIN,
	SINGLE_FILE_SCAN_END,
	SCAN_END,
	SYMBOL_INSERTED,
	SYMBOL_UPDATED,
	SYMBOL_SCOPE_UPDATED,
	SYMBOL_REMOVED,
	LAST_SIGNAL
};

/*
 * enums
 */
enum {
	DO_UPDATE_SYMS = 1,
	DO_UPDATE_SYMS_AND_EXIT,
	DONT_UPDATE_SYMS,
	DONT_UPDATE_SYMS_AND_EXIT,
	DONT_FAKE_UPDATE_SYMS,
	END_UPDATE_GROUP_SYMS
};

/*
 * structs used for callback data.
 */
typedef struct _UpdateFileSymbolsData {	
	gchar *project;
	gchar *project_directory;
	gboolean update_prj_analyse_time;
	GPtrArray * files_path;
	
} UpdateFileSymbolsData;

typedef struct _ScanFiles1Data {
	SymbolDBEngine *dbe;
	
	gchar *real_file;	/* may be NULL. If not NULL must be freed */
	gint partial_count;
	gint files_list_len;
	gint symbols_update;
	
} ScanFiles1Data;

/*
 * global file variables
 */ 
static GObjectClass *parent_class = NULL;
static unsigned int signals[LAST_SIGNAL] = { 0 };


#ifdef DEBUG
static GTimer *sym_timer_DEBUG  = NULL;
static gint tags_total_DEBUG = 0;
static gdouble elapsed_total_DEBUG = 0;
#endif

/*
 * forward declarations 
 */
static void 
sdb_engine_second_pass_do (SymbolDBEngine * dbe);

static gint
sdb_engine_add_new_symbol (SymbolDBEngine * dbe, const tagEntry * tag_entry,
						   int file_defined_id, gboolean sym_update);

const GdaStatement *
sdb_engine_get_statement_by_query_id (SymbolDBEngine * dbe, static_query_type query_id);

GQuark
symbol_db_engine_error_quark (void)
{
	return g_quark_from_static_string ("symbol-db-engine-error-quark");
}

/*
 * implementation starts here 
 */
static GNUC_INLINE gint
sdb_engine_cache_lookup (GHashTable* hash_table, const gchar* lookup)
{
	gpointer orig_key = NULL;
	gpointer value = NULL;
	
	/* avoiding lazy initialization may gain some cpu cycles. Just lookup here. */	
	if (g_hash_table_lookup_extended (hash_table, 
									  lookup,
									  &orig_key,
									  &value))
	{
		gint table_id = GPOINTER_TO_INT (value);
		return table_id;
	}
	return -1;
}

static GNUC_INLINE void
sdb_engine_insert_cache (GHashTable* hash_table, const gchar* key,
						 gint value)
{
	g_hash_table_insert (hash_table, g_strdup (key), 
						 GINT_TO_POINTER (value));
}

static void
sdb_engine_tablemap_tmp_heritage_destroy (TableMapTmpHeritage *node)
{
	g_free (node->field_inherits);
	g_free (node->field_struct);
	g_free (node->field_typeref);
	g_free (node->field_enum);
	g_free (node->field_union);
	g_free (node->field_class);
	g_free (node->field_namespace);

	g_slice_free (TableMapTmpHeritage, node);
}

static void
sdb_engine_scan_data_destroy (gpointer data)
{
	EngineScanDataAsync *esda =  (EngineScanDataAsync *)data;

	g_ptr_array_unref (esda->files_list);
	if (esda->real_files_list)
		g_ptr_array_unref (esda->real_files_list);

	g_free (esda);
}

static void
sdb_engine_clear_tablemaps (SymbolDBEngine *dbe)
{
	SymbolDBEnginePriv *priv = dbe->priv;
	if (priv->tmp_heritage_tablemap)
	{
		TableMapTmpHeritage *node;
		while ((node = g_queue_pop_head (priv->tmp_heritage_tablemap)) != NULL)
		{
			sdb_engine_tablemap_tmp_heritage_destroy (node);
		}

		/* queue should be void. Free it */
		g_queue_free (priv->tmp_heritage_tablemap);
		priv->tmp_heritage_tablemap = NULL;
	}
}

static void
sdb_engine_clear_caches (SymbolDBEngine* dbe)
{
	SymbolDBEnginePriv *priv = dbe->priv;
	if (priv->kind_cache)
		g_hash_table_destroy (priv->kind_cache);
	if (priv->access_cache)
		g_hash_table_destroy (priv->access_cache);	
	if (priv->implementation_cache)
		g_hash_table_destroy (priv->implementation_cache);
	if (priv->language_cache)
		g_hash_table_destroy (priv->language_cache);
	
	priv->kind_cache = NULL;
	priv->access_cache = NULL;
	priv->implementation_cache = NULL;
	priv->language_cache = NULL;
}

static void
sdb_engine_init_table_maps (SymbolDBEngine *dbe)
{
	SymbolDBEnginePriv *priv = dbe->priv;

	/* tmp_heritage_tablemap */
	priv->tmp_heritage_tablemap = g_queue_new ();
}

static void
sdb_engine_init_caches (SymbolDBEngine* dbe)
{	
	SymbolDBEnginePriv *priv = dbe->priv;
	priv->kind_cache = g_hash_table_new_full (g_str_hash,
											g_str_equal,
											g_free,
											NULL);
	
	priv->access_cache = g_hash_table_new_full (g_str_hash,
											g_str_equal,
											g_free,
											NULL);
	
	priv->implementation_cache = g_hash_table_new_full (g_str_hash,
											g_str_equal,
											g_free,
											NULL);	

	priv->language_cache = g_hash_table_new_full (g_str_hash,
	    									g_str_equal,
	    									g_free,
	    									NULL);
}

/* ~~~ Thread note: this function locks the mutex ~~~ */ 
static gboolean 
sdb_engine_execute_unknown_sql (SymbolDBEngine *dbe, const gchar *sql)
{
	GdaStatement *stmt;
	GObject *res;
	SymbolDBEnginePriv *priv;
	
	priv = dbe->priv;	
	
	SDB_LOCK(priv);
	stmt = gda_sql_parser_parse_string (priv->sql_parser, sql, NULL, NULL);	

	if (stmt == NULL)
	{
		SDB_UNLOCK(priv);
		return FALSE;
	}
	
    if ((res = gda_connection_statement_execute (priv->db_connection, 
												   (GdaStatement*)stmt, 
												 	NULL,
													GDA_STATEMENT_MODEL_RANDOM_ACCESS, 
													NULL, NULL)) == NULL)
	{
		g_object_unref (stmt);
		SDB_UNLOCK(priv);
		return FALSE;
	}
	else 
	{
		g_object_unref (res);
		g_object_unref (stmt);
		SDB_UNLOCK(priv);
		return TRUE;
	}
}

static GdaDataModel *
sdb_engine_execute_select_sql (SymbolDBEngine * dbe, const gchar *sql)
{
	GdaStatement *stmt;
	GdaDataModel *res;
	SymbolDBEnginePriv *priv;
	const gchar *remain;
	
	priv = dbe->priv;	
	
	stmt = gda_sql_parser_parse_string (priv->sql_parser, sql, &remain, NULL);	

	if (stmt == NULL)
		return NULL;
	
	res = gda_connection_statement_execute_select (priv->db_connection, 
												   (GdaStatement*)stmt, NULL, NULL);
	if (!res) 
		DEBUG_PRINT ("Could not execute query: %s\n", sql);
	
	if (remain != NULL)
	{
		/* this shouldn't never happen */		
		sdb_engine_execute_select_sql (dbe, remain);
	}	
	
	g_object_unref (stmt);
	
	return res;
}

static gint
sdb_engine_execute_non_select_sql (SymbolDBEngine * dbe, const gchar *sql)
{
	GdaStatement *stmt;
    gint nrows;
	SymbolDBEnginePriv *priv;
	const gchar *remain;	
	
	priv = dbe->priv;
	stmt = gda_sql_parser_parse_string (priv->sql_parser, 
										sql, &remain, NULL);

	if (stmt == NULL)
		return -1;
	
	nrows = gda_connection_statement_execute_non_select (priv->db_connection, stmt, 
														 NULL, NULL, NULL);
	if (remain != NULL) {
		/* may happen for example when sql is a file-content */
		sdb_engine_execute_non_select_sql (dbe, remain);
	}
	
	g_object_unref (stmt);
	return nrows;
}

/**
 * ### Thread note: this function inherits the mutex lock ### 
 *
 * Use a proxy to return an already present or a fresh new prepared query 
 * from static 'query_list'. We should perform actions in the fastest way, because
 * these queries are time-critical.
 * A GdaSet will also be populated once, avoiding so to create again later on.
 */
const GdaStatement *
sdb_engine_get_statement_by_query_id (SymbolDBEngine * dbe, static_query_type query_id)
{
	static_query_node *node;
	SymbolDBEnginePriv *priv;

	priv = dbe->priv;

	/* no way: if connection is NULL we will break here. There must be
	 * a connection established to db before using this function */
	/*g_return_val_if_fail (priv->db_connection != NULL, NULL);*/
	
	if ((node = priv->static_query_list[query_id]) == NULL)
		return NULL;

	if (node->stmt == NULL)
	{
		GError *error = NULL;
		
		/* create a new GdaStatement */
		node->stmt =
			gda_sql_parser_parse_string (priv->sql_parser, node->query_str, NULL, 
										 &error);

		if (error)
		{
			g_warning ("%s", error->message);
			g_error_free (error);
			return NULL;
		}

		if (gda_statement_get_parameters ((GdaStatement*)node->stmt, 
										  &node->plist, NULL) == FALSE)
		{
			g_warning ("Error on getting parameters for %d", query_id);
		}
	}

	return node->stmt;
}

/**
 * ### Thread note: this function inherits the mutex lock ### 
 *
 * Return a GdaSet of parameters calculated from the statement. It does not check
 * if it's null. You *must* be sure to have called sdb_engine_get_statement_by_query_id () first.
 */
static GNUC_INLINE const GdaSet *
sdb_engine_get_query_parameters_list (SymbolDBEngine *dbe, static_query_type query_id)
{
	SymbolDBEnginePriv *priv;
	
	priv = dbe->priv;
	
	static_query_node *node;
	node = priv->static_query_list[query_id];
	return node->plist;
}

/**
 * Clear the static cached queries data. You should call this function when closing/
 * destroying SymbolDBEngine object.
 */
static void
sdb_engine_free_cached_queries (SymbolDBEngine *dbe)
{
	SymbolDBEnginePriv *priv;
	gint i;
	static_query_node *node;
	
	priv = dbe->priv;

	for (i = 0; i < PREP_QUERY_COUNT; i++)
	{
		node = priv->static_query_list[i];

		if (node != NULL && node->stmt != NULL)
		{
			/*DEBUG_PRINT ("sdb_engine_free_cached_queries stmt %d", i);*/
			g_object_unref (node->stmt);
			node->stmt = NULL;
		}
		
		if (node != NULL && node->plist != NULL)
		{
			/*DEBUG_PRINT ("sdb_engine_free_cached_queries plist %d", i);*/
			g_object_unref (node->plist);
			node->plist = NULL;
		}
		
		/* last but not the least free the node itself */
		g_free (node);
		priv->static_query_list[i] = NULL;
	}
}

static gboolean
sdb_engine_disconnect_from_db (SymbolDBEngine * dbe)
{
	SymbolDBEnginePriv *priv;

	g_return_val_if_fail (dbe != NULL, FALSE);
	priv = dbe->priv;

	DEBUG_PRINT ("VACUUM command issued on %s", priv->cnc_string);
	sdb_engine_execute_non_select_sql (dbe, "VACUUM");
	
	DEBUG_PRINT ("Disconnecting from %s", priv->cnc_string);
	g_free (priv->cnc_string);
	priv->cnc_string = NULL;

	if (priv->db_connection != NULL) {
#ifdef HAVE_GDA6
		GError* err = NULL;
		gda_connection_close (priv->db_connection, &err);
		if (err != NULL)
			return FALSE;
#else
		gda_connection_close (priv->db_connection);
#endif
	}
	priv->db_connection = NULL;

	if (priv->sql_parser != NULL)
		g_object_unref (priv->sql_parser);
	priv->sql_parser = NULL;
	
	return TRUE;
}

/**
 * ### Thread note: this function inherits the mutex lock ### 
 *
 * @return -1 on error. Otherwise the id of tuple.
 */
static GNUC_INLINE gint
sdb_engine_get_tuple_id_by_unique_name (SymbolDBEngine * dbe, static_query_type qtype,
										gchar * param_key,
										GValue * param_value)
{
	const GdaSet *plist;
	const GdaStatement *stmt;
	GdaHolder *param;
	GdaDataModel *data_model;
	const GValue *num;
	gint table_id;
	SymbolDBEnginePriv *priv;
	
	priv = dbe->priv;

	/* get prepared query */
	if ((stmt = sdb_engine_get_statement_by_query_id (dbe, qtype)) == NULL)
	{
		g_warning ("Query is null");
		return -1;
	}

	plist = sdb_engine_get_query_parameters_list (dbe, qtype);
	
	if ((param = gda_set_get_holder ((GdaSet*)plist, param_key)) == NULL)
	{
		g_warning ("sdb_engine_get_tuple_id_by_unique_name: param is NULL "
				   "from pquery!\n");
		return -1;
	}

	gda_holder_set_value (param, param_value, NULL);
	
	/* execute the query with parameters just set */
	data_model = gda_connection_statement_execute_select (priv->db_connection, 
														  (GdaStatement*)stmt, 
														  (GdaSet*)plist, NULL);
		
	if (!GDA_IS_DATA_MODEL (data_model) ||
		gda_data_model_get_n_rows (GDA_DATA_MODEL (data_model)) <= 0)
	{
		if (data_model != NULL)
			g_object_unref (data_model);
		return -1;
	}

	/* get and parse the results. */
	num = gda_data_model_get_value_at (GDA_DATA_MODEL (data_model), 0, 0, NULL);

	table_id = g_value_get_int (num);
	g_object_unref (data_model);

	/* set the value to a dummy string because we won't use the real value anymore */
	return table_id;
}

/* ### Thread note: this function inherits the mutex lock ### */
static GNUC_INLINE gint
sdb_engine_get_tuple_id_by_unique_name5 (SymbolDBEngine * dbe, 
										 static_query_type qtype,
										 gchar * param_key1,
										 GValue * value1,
										 gchar * param_key2,
										 GValue * value2,
										 gchar * param_key3,
										 GValue * value3,
										 gchar * param_key4,
										 GValue * value4,
										 gchar * param_key5,
										 GValue * value5)
{
	const GdaSet *plist;
	const GdaStatement *stmt;
	GdaHolder *param;
	GdaDataModel *data_model;
	const GValue *num;
	gint table_id;
	SymbolDBEnginePriv *priv;

	priv = dbe->priv;

	/* get prepared query */
	if ((stmt = sdb_engine_get_statement_by_query_id (dbe, qtype)) == NULL)
	{
		g_warning ("Query is null");
		return -1;
	}

	plist = sdb_engine_get_query_parameters_list (dbe, qtype);
	
	if ((param = gda_set_get_holder ((GdaSet*)plist, param_key1)) == NULL)
	{
		g_warning ("sdb_engine_get_tuple_id_by_unique_name5: param is NULL "
				   "from pquery!\n");
		return -1;
	}
	
	gda_holder_set_value (param, value1, NULL);	

	/* ...and the second one */
	if ((param = gda_set_get_holder ((GdaSet*)plist, param_key2)) == NULL)
	{
		g_warning ("sdb_engine_get_tuple_id_by_unique_name5: "
				   "param is NULL from pquery!");
		return -1;
	}
	
	gda_holder_set_value (param, value2, NULL);	

	/* ...and the third one */
	if ((param = gda_set_get_holder ((GdaSet*)plist, param_key3)) == NULL)
	{
		g_warning ("sdb_engine_get_tuple_id_by_unique_name5: "
				   "param is NULL from pquery!");
		return -1;
	}
	
	gda_holder_set_value (param, value3, NULL);	
		
	/* ...and the fourth one */
	if ((param = gda_set_get_holder ((GdaSet*)plist, param_key4)) == NULL)
	{
		g_warning ("sdb_engine_get_tuple_id_by_unique_name5: "
				   "param is NULL from pquery!");
		return -1;
	}

	gda_holder_set_value (param, value4, NULL);	

	/* ...and the fifth one */
	if ((param = gda_set_get_holder ((GdaSet*)plist, param_key5)) == NULL)
	{
		g_warning ("sdb_engine_get_tuple_id_by_unique_name5: "
		           "param is NULL from pquery!");
		return -1;
	}

	gda_holder_set_value (param, value5, NULL);

	/* execute the query with parameters just set */
	data_model = gda_connection_statement_execute_select (priv->db_connection, 
														  (GdaStatement*)stmt, 
														  (GdaSet*)plist, NULL);
		
	if (!GDA_IS_DATA_MODEL (data_model) ||
		gda_data_model_get_n_rows (GDA_DATA_MODEL (data_model)) <= 0)
	{
		if (data_model != NULL)
			g_object_unref (data_model);
	
		return -1;
	}

	/* get and parse the results. */
	num = gda_data_model_get_value_at (GDA_DATA_MODEL (data_model), 0, 0, NULL);

	table_id = g_value_get_int (num);
	g_object_unref (data_model);
	return table_id;
}

/** ### Thread note: this function inherits the mutex lock ### */
static int
sdb_engine_get_file_defined_id (SymbolDBEngine* dbe,
								const gchar* base_prj_path,
								const gchar* fake_file_on_db,
								tagEntry* tag_entry)
{
	GValue v = {0};

	gint file_defined_id = 0;
	if (base_prj_path != NULL && g_str_has_prefix (tag_entry->file, base_prj_path))
	{
		/* in this case fake_file will be ignored. */
		
		/* we expect here an absolute path */
		SDB_GVALUE_SET_STATIC_STRING(v, tag_entry->file + strlen (base_prj_path));
	}
	else
	{
		/* check whether the fake_file can substitute the tag_entry->file one */
		if (fake_file_on_db == NULL)
		{
			SDB_GVALUE_SET_STATIC_STRING(v, tag_entry->file);
		}
		else
		{
			SDB_GVALUE_SET_STATIC_STRING(v, fake_file_on_db);
		}
	}
	
	if ((file_defined_id = sdb_engine_get_tuple_id_by_unique_name (dbe,
													PREP_QUERY_GET_FILE_ID_BY_UNIQUE_NAME,
													"filepath",
													&v)) < 0)
	{	
		/* if we arrive here there should be some sync problems between the filenames
		 * in database and the ones in the ctags files. We trust in db's ones,
		 * so we'll just return here.
		 */
		g_warning ("sync problems between db and ctags filenames entries. "
				   "File was %s (base_path: %s, fake_file: %s, tag_file: %s)", 
				   g_value_get_string (&v), base_prj_path, fake_file_on_db, 
				   tag_entry->file);
		return -1;
	}

	return file_defined_id;
}

/**
 * ### Thread note: this function inherits the mutex lock ###
 *
 * If fake_file is != NULL we claim and assert that tags contents which are
 * scanned belong to the fake_file in the project.
 * More: the fake_file refers to just one single file and cannot be used
 * for multiple fake_files.
 */
static void
sdb_engine_populate_db_by_tags (SymbolDBEngine * dbe, FILE* fd,
								gchar * fake_file_on_db,
								gboolean force_sym_update)
{
	tagFile *tag_file;
	tagFileInfo tag_file_info;
	tagEntry tag_entry;
	gint file_defined_id_cache = 0;
	gchar* tag_entry_file_cache = NULL;
	
	SymbolDBEnginePriv *priv = dbe->priv;

	gchar* base_prj_path = fake_file_on_db == NULL ?
		priv->project_directory : NULL;
	
	g_return_if_fail (dbe != NULL);

	g_return_if_fail (priv->db_connection != NULL);
	g_return_if_fail (fd != NULL);
	
	if ((tag_file = tagsOpen_1 (fd, &tag_file_info)) == NULL)
	{
		g_warning ("error in opening ctags file");
	}

#ifdef DEBUG
	if (sym_timer_DEBUG == NULL)
		sym_timer_DEBUG = g_timer_new ();
	else
		g_timer_reset (sym_timer_DEBUG);	
	gint tags_num_DEBUG = 0;
#endif	
	tag_entry.file = NULL;

	while (tagsNext (tag_file, &tag_entry) != TagFailure)
	{
		gint file_defined_id = 0;
		if (tag_entry.file == NULL)
		{
			continue;
		}
		if (file_defined_id_cache > 0)
		{
			if (g_str_equal (tag_entry.file, tag_entry_file_cache))
			{
				file_defined_id = file_defined_id_cache;
			}
		}
		if (file_defined_id == 0)
		{
			file_defined_id = sdb_engine_get_file_defined_id (dbe,
															  base_prj_path,
															  fake_file_on_db,
															  &tag_entry);
			file_defined_id_cache = file_defined_id;
			g_free (tag_entry_file_cache);
			tag_entry_file_cache = g_strdup (tag_entry.file);
		}
		
		if (priv->symbols_scanned_count++ % BATCH_SYMBOL_NUMBER == 0)
		{
			GError *error = NULL;
			
			/* if we aren't at the first cycle then we can commit the transaction */
			if (priv->symbols_scanned_count > 1)
			{
				gda_connection_commit_transaction (priv->db_connection, "symboltrans",
					&error);

				if (error)
				{
					DEBUG_PRINT ("err: %s", error->message);
					g_error_free (error);
					error = NULL;
				}
			}
			
			gda_connection_begin_transaction (priv->db_connection, "symboltrans",
						GDA_TRANSACTION_ISOLATION_READ_UNCOMMITTED, &error);
			
			if (error)
			{
				DEBUG_PRINT ("err: %s", error->message);
				g_error_free (error);
				error = NULL;
			}			
		}
		
		/* insert or update a symbol */
		sdb_engine_add_new_symbol (dbe, &tag_entry, file_defined_id,
								   force_sym_update);
#ifdef DEBUG
		tags_num_DEBUG++;
#endif		
		tag_entry.file = NULL;
	}
	g_free (tag_entry_file_cache);
	
	
#ifdef DEBUG
	gdouble elapsed_DEBUG = g_timer_elapsed (sym_timer_DEBUG, NULL);
	tags_total_DEBUG += tags_num_DEBUG;
	elapsed_total_DEBUG += elapsed_DEBUG;
/*	DEBUG_PRINT ("elapsed: %f for (%d) [%f sec/symbol] [av %f sec/symbol]", elapsed_DEBUG,
				 tags_num_DEBUG, elapsed_DEBUG / tags_num_DEBUG, 
				 elapsed_total_DEBUG / tags_total_DEBUG);
*/				 
#endif
	
	/* notify listeners that another file has been scanned */
	DBESignal *dbesig = g_slice_new0 (DBESignal);
	dbesig->value = GINT_TO_POINTER (SINGLE_FILE_SCAN_END +1);
	dbesig->process_id = priv->current_scan_process_id;
	
	g_async_queue_push (priv->signals_aqueue, dbesig);
	
	/* we've done with tag_file but we don't need to tagsClose (tag_file); */
}

/* ~~~ Thread note: this function locks the mutex ~~~ */ 
static void
sdb_engine_ctags_output_thread (gpointer data, gpointer user_data)
{
	gint len_chars;
	gchar *chars, *chars_ptr;
	gint remaining_chars;
	gint len_marker;
	SymbolDBEnginePriv *priv;
	SymbolDBEngine *dbe;
	
	chars = chars_ptr = (gchar *)data;
	dbe = SYMBOL_DB_ENGINE (user_data);
	
	g_return_if_fail (dbe != NULL);	
	g_return_if_fail (chars_ptr != NULL);

	priv = dbe->priv;

	SDB_LOCK(priv);
	
	remaining_chars = len_chars = strlen (chars_ptr);
	len_marker = strlen (CTAGS_MARKER);	

	/*DEBUG_PRINT ("program output [new version]: ==>%s<==", chars);*/
	if (len_chars >= len_marker) 
	{
		gchar *marker_ptr = NULL;
		gint tmp_str_length = 0;

		/* is it an end file marker? */
		marker_ptr = strstr (chars_ptr, CTAGS_MARKER);

		do  
		{
			if (marker_ptr != NULL) 
			{
				int scan_flag;
				gchar *real_file;
		
				/* set the length of the string parsed */
				tmp_str_length = marker_ptr - chars_ptr;
	
				/* write to shm_file all the chars_ptr received without the marker ones */
				fwrite (chars_ptr, sizeof(gchar), tmp_str_length, priv->shared_mem_file);

				chars_ptr = marker_ptr + len_marker;
				remaining_chars -= (tmp_str_length + len_marker);
				fflush (priv->shared_mem_file);
				
				/* get the scan flag from the queue. We need it to know whether
				 * an update of symbols must be done or not */
				DBESignal *dbesig = g_async_queue_try_pop (priv->scan_aqueue);
				scan_flag = GPOINTER_TO_INT(dbesig->value);
				g_slice_free (DBESignal, dbesig);

				dbesig = g_async_queue_try_pop (priv->scan_aqueue);
				real_file = dbesig->value;
				g_slice_free (DBESignal, dbesig);
				
				/* and now call the populating function */
				if (scan_flag == DO_UPDATE_SYMS ||
					scan_flag == DO_UPDATE_SYMS_AND_EXIT)
				{
					sdb_engine_populate_db_by_tags (dbe, priv->shared_mem_file,
								(gsize)real_file == DONT_FAKE_UPDATE_SYMS ? NULL : real_file, 
								TRUE);
				}
				else 
				{
					sdb_engine_populate_db_by_tags (dbe, priv->shared_mem_file,
								(gsize)real_file == DONT_FAKE_UPDATE_SYMS ? NULL : real_file, 
								FALSE);					
				}
				
				/* don't forget to free the real_file, if it's a char */
				if ((gsize)real_file != DONT_FAKE_UPDATE_SYMS)
					g_free (real_file);
				
				/* check also if, together with an end file marker, we have an 
				 * end group-of-files end marker.
				 */
				if (scan_flag == DO_UPDATE_SYMS_AND_EXIT || 
					 scan_flag == DONT_UPDATE_SYMS_AND_EXIT )
				{
					gint tmp_inserted;
					gint tmp_updated;

					/* scan has ended. Go go with second step. */
					DEBUG_PRINT ("%s", "FOUND end-of-group-files marker.");
					
					chars_ptr += len_marker;
					remaining_chars -= len_marker;
					
					/* will emit symbol_scope_updated and will flush on disk 
					 * tablemaps
					 */
					sdb_engine_second_pass_do (dbe);					
					
					/* Here we are. It's the right time to notify the listeners
					 * about out fresh new inserted/updated symbols...
					 * Go on by emitting them.
					 */
					while ((tmp_inserted = GPOINTER_TO_INT(
							g_async_queue_try_pop (priv->inserted_syms_id_aqueue))) > 0)
					{
						/* we must be sure to insert both signals at once */
						g_async_queue_lock (priv->signals_aqueue);

						DBESignal *dbesig1 = g_slice_new0 (DBESignal);
						DBESignal *dbesig2 = g_slice_new0 (DBESignal);
						
						dbesig1->value = GINT_TO_POINTER (SYMBOL_INSERTED + 1);
						dbesig1->process_id = priv->current_scan_process_id;

						dbesig2->value = GINT_TO_POINTER (tmp_inserted);
						dbesig2->process_id = priv->current_scan_process_id;
						
						g_async_queue_push_unlocked (priv->signals_aqueue, 
													 dbesig1);
						g_async_queue_push_unlocked (priv->signals_aqueue, 
													 dbesig2);
						
						g_async_queue_unlock (priv->signals_aqueue);
					}
						
					while ((tmp_updated = GPOINTER_TO_INT(
							g_async_queue_try_pop (priv->updated_syms_id_aqueue))) > 0)
					{
						g_async_queue_lock (priv->signals_aqueue);

						DBESignal *dbesig1 = g_slice_new0 (DBESignal);
						DBESignal *dbesig2 = g_slice_new0 (DBESignal);

						dbesig1->value = GINT_TO_POINTER (SYMBOL_UPDATED + 1);
						dbesig1->process_id = priv->current_scan_process_id;

						dbesig2->value = GINT_TO_POINTER (tmp_updated);
						dbesig2->process_id = priv->current_scan_process_id;
						
						g_async_queue_push_unlocked (priv->signals_aqueue, dbesig1);
						g_async_queue_push_unlocked (priv->signals_aqueue, dbesig2);
						g_async_queue_unlock (priv->signals_aqueue);
					}

					while ((tmp_updated = GPOINTER_TO_INT(
							g_async_queue_try_pop (priv->updated_scope_syms_id_aqueue))) > 0)
					{
						g_async_queue_lock (priv->signals_aqueue);

						DBESignal *dbesig1 = g_slice_new0 (DBESignal);
						DBESignal *dbesig2 = g_slice_new0 (DBESignal);

						dbesig1->value = GINT_TO_POINTER (SYMBOL_SCOPE_UPDATED + 1);
						dbesig1->process_id = priv->current_scan_process_id;

						dbesig2->value = GINT_TO_POINTER (tmp_updated);
						dbesig2->process_id = priv->current_scan_process_id;
						
						g_async_queue_push_unlocked (priv->signals_aqueue, dbesig1);
						g_async_queue_push_unlocked (priv->signals_aqueue, dbesig2);
						g_async_queue_unlock (priv->signals_aqueue);
					}		
										
#ifdef DEBUG	
					if (priv->first_scan_timer_DEBUG != NULL)
					{
						DEBUG_PRINT ("~~~~~ TOTAL FIRST SCAN elapsed: %f ",
						    g_timer_elapsed (priv->first_scan_timer_DEBUG, NULL));
						g_timer_destroy (priv->first_scan_timer_DEBUG);
						priv->first_scan_timer_DEBUG = NULL;
					}
#endif
		
					DBESignal *dbesig1 = g_slice_new0 (DBESignal);

					dbesig1->value = GINT_TO_POINTER (SCAN_END + 1);
					dbesig1->process_id = priv->current_scan_process_id;
					
					g_async_queue_push (priv->signals_aqueue, dbesig1);
				}
				
				/* truncate the file to 0 length */
				ftruncate (priv->shared_mem_fd, 0);				
			}
			else
			{ 
				/* marker_ptr is NULL here. We should then exit the loop. */
				/* write to shm_file all the chars received */
				fwrite (chars_ptr, sizeof(gchar), remaining_chars, 
						priv->shared_mem_file);

				fflush (priv->shared_mem_file);
				break;
			}

			/* found out a new marker */ 
			marker_ptr = strstr (marker_ptr + len_marker, CTAGS_MARKER);
		} while (remaining_chars + len_marker < len_chars || marker_ptr != NULL);
	}
	
	SDB_UNLOCK(priv);
	
	g_free (chars);
}


/**
 * This function runs on the main glib thread, so that it can safely spread signals 
 */
static gboolean
sdb_engine_timeout_trigger_signals (gpointer user_data)
{
	SymbolDBEngine *dbe = (SymbolDBEngine *) user_data;
	SymbolDBEnginePriv *priv;

	g_return_val_if_fail (user_data != NULL, FALSE);	
	priv = dbe->priv;		

	if (priv->signals_aqueue != NULL && 
	    g_async_queue_length (priv->signals_aqueue) > 0)
	{
		DBESignal *dbesig;
		gsize real_signal;
		gint process_id;

		while (priv->signals_aqueue != NULL &&
		    (dbesig = g_async_queue_try_pop (priv->signals_aqueue)) != NULL)
		{
			if (dbesig == NULL)
			{
				return g_async_queue_length (priv->signals_aqueue) > 0 ? TRUE : FALSE;
			}

			real_signal = GPOINTER_TO_INT (dbesig->value) -1;
			process_id = dbesig->process_id;
			
			switch (real_signal) 
			{
				case SINGLE_FILE_SCAN_END:
				{
					g_signal_emit (dbe, signals[SINGLE_FILE_SCAN_END], 0);
				}
					break;

				case SCAN_BEGIN:
				{
					DEBUG_PRINT ("%s", "EMITTING scan begin.");
					g_signal_emit (dbe, signals[SCAN_BEGIN], 0, process_id);
				}
					break;
					
				case SCAN_END:
				{
					/* reset count */
					priv->symbols_scanned_count = 0;

					DEBUG_PRINT ("Committing symboltrans transaction...");
					gda_connection_commit_transaction (priv->db_connection, "symboltrans",
		    						NULL);
					DEBUG_PRINT ("... Done!");
					
					/* perform flush on db of the tablemaps, if this is the 1st scan */
					if (priv->is_first_population == TRUE)
					{
						/* ok, set the flag to false. We're done with it */
						priv->is_first_population = FALSE;
					}

					priv->is_scanning = FALSE;

					DEBUG_PRINT ("%s", "EMITTING scan-end");
					g_signal_emit (dbe, signals[SCAN_END], 0, process_id);
				}
					break;
	
				case SYMBOL_INSERTED:
				{
					DBESignal *dbesig2; 
					
					dbesig2 = g_async_queue_try_pop (priv->signals_aqueue);
					g_signal_emit (dbe, signals[SYMBOL_INSERTED], 0, dbesig2->value);

					g_slice_free (DBESignal, dbesig2);
				}
					break;
	
				case SYMBOL_UPDATED:
				{
					DBESignal *dbesig2;
					
					dbesig2 = g_async_queue_try_pop (priv->signals_aqueue);
					g_signal_emit (dbe, signals[SYMBOL_UPDATED], 0, dbesig2->value);

					g_slice_free (DBESignal, dbesig2);
				}
					break;
	
				case SYMBOL_SCOPE_UPDATED:
				{
					DBESignal *dbesig2;
					
					dbesig2 = g_async_queue_try_pop (priv->signals_aqueue);
					g_signal_emit (dbe, signals[SYMBOL_SCOPE_UPDATED], 0, dbesig2->value);

					g_slice_free (DBESignal, dbesig2);
				}
					break;
	
				case SYMBOL_REMOVED:
				{
					DBESignal *dbesig2;
					
					dbesig2 = g_async_queue_try_pop (priv->signals_aqueue);
					g_signal_emit (dbe, signals[SYMBOL_REMOVED], 0, dbesig2->value);

					g_slice_free (DBESignal, dbesig2);					
				}
					break;
			}

			g_slice_free (DBESignal, dbesig);
		}
		/* reset to 0 the retries */
		priv->trigger_closure_retries = 0;
	}
	else {
		priv->trigger_closure_retries++;
	}
	
	if (priv->thread_pool != NULL &&
	    g_thread_pool_unprocessed (priv->thread_pool) == 0 &&
		g_thread_pool_get_num_threads (priv->thread_pool) == 0)
	{
		/* remove the trigger coz we don't need it anymore... */
		g_source_remove (priv->timeout_trigger_handler);
		priv->timeout_trigger_handler = 0;
		return FALSE;
	}
	
	return TRUE;
}

static void
sdb_engine_ctags_output_callback_1 (AnjutaLauncher * launcher,
								  AnjutaLauncherOutputType output_type,
								  const gchar * chars, gpointer user_data)
{
	SymbolDBEngine *dbe = (SymbolDBEngine *) user_data;
	SymbolDBEnginePriv *priv;

	g_return_if_fail (user_data != NULL);
	
	priv = dbe->priv;	
	
	if (priv->shutting_down == TRUE)
		return;

	g_thread_pool_push (priv->thread_pool, g_strdup (chars), NULL);
	
	/* signals monitor */
	if (priv->timeout_trigger_handler <= 0)
	{
		priv->timeout_trigger_handler = 
			g_timeout_add_full (G_PRIORITY_DEFAULT_IDLE, TRIGGER_SIGNALS_DELAY, 
						   sdb_engine_timeout_trigger_signals, user_data, NULL);
		priv->trigger_closure_retries = 0;
	}
}

static void
on_scan_files_end_1 (AnjutaLauncher * launcher, int child_pid,
				   int exit_status, gulong time_taken_in_seconds,
				   gpointer user_data)
{
	SymbolDBEngine *dbe = (SymbolDBEngine *) user_data;
	SymbolDBEnginePriv *priv;

	g_return_if_fail (user_data != NULL);
	
	priv = dbe->priv;	
	
	DEBUG_PRINT ("***** ctags ended (%s) (%s) *****", priv->ctags_path, 
	    priv->cnc_string);
	
	if (priv->shutting_down == TRUE)
		return;

	priv->ctags_path = NULL;
}

static void
sdb_engine_ctags_launcher_create (SymbolDBEngine * dbe)
{
	SymbolDBEnginePriv *priv;
	gchar *exe_string;
		
	priv = dbe->priv;
	
	DEBUG_PRINT ("Creating anjuta_launcher with %s for %s", priv->ctags_path, 
					priv->cnc_string);

	priv->ctags_launcher = anjuta_launcher_new ();

	anjuta_launcher_set_check_passwd_prompt (priv->ctags_launcher, FALSE);
	anjuta_launcher_set_encoding (priv->ctags_launcher, NULL);
		
	g_signal_connect (G_OBJECT (priv->ctags_launcher), "child-exited",
						  G_CALLBACK (on_scan_files_end_1), dbe);

	exe_string = g_strdup_printf ("%s --sort=no --fields=afmiKlnsStTz --c++-kinds=+p "
								  "--filter=yes --filter-terminator='"CTAGS_MARKER"'",
								  priv->ctags_path);
	DEBUG_PRINT ("Launching %s", exe_string);
	anjuta_launcher_execute (priv->ctags_launcher,
								 exe_string, sdb_engine_ctags_output_callback_1, 
								 dbe);
	g_free (exe_string);
}

/**
 * A GAsyncReadyCallback function. This function is the async continuation for
 * sdb_engine_scan_files_1 ().
 */
static void  
sdb_engine_scan_files_2 (GFile *gfile,
                         GAsyncResult *res,
                         gpointer user_data)
{
	ScanFiles1Data *sf_data = (ScanFiles1Data*)user_data;
	SymbolDBEngine *dbe;
	SymbolDBEnginePriv *priv;
	GFileInfo *ginfo;
	gchar *local_path;
	gchar *real_file;
	gboolean symbols_update;
	gint partial_count;
	gint files_list_len;

	dbe = sf_data->dbe;
	symbols_update = sf_data->symbols_update;
	real_file = sf_data->real_file;
	files_list_len = sf_data->files_list_len;
	partial_count = sf_data->partial_count;

	priv = dbe->priv;
	
	ginfo = g_file_query_info_finish (gfile, res, NULL);

	local_path = g_file_get_path (gfile);
	
	if (ginfo == NULL || 
		g_file_info_get_attribute_boolean (ginfo, 
								   G_FILE_ATTRIBUTE_ACCESS_CAN_READ) == FALSE)
	{
		g_warning ("File does not exist or is unreadable by user (%s)", local_path);

		g_free (local_path);
		g_free (real_file);
		g_free (sf_data);

		if (ginfo)
			g_object_unref (ginfo);
		if (gfile)
			g_object_unref (gfile);
		return;
	}
	
	/* DEBUG_PRINT ("sent to stdin %s", local_path); */
	anjuta_launcher_send_stdin (priv->ctags_launcher, local_path);
	anjuta_launcher_send_stdin (priv->ctags_launcher, "\n");
	
	if (symbols_update == TRUE) 
	{
		DBESignal *dbesig;
		
		/* will this be the last file in the list? */
		if (partial_count + 1 >= files_list_len) 
		{
			dbesig = g_slice_new0 (DBESignal);
			dbesig->value = GINT_TO_POINTER (DO_UPDATE_SYMS_AND_EXIT);
			dbesig->process_id = priv->current_scan_process_id;
			
			/* yes */
			g_async_queue_push (priv->scan_aqueue, dbesig);
		}
		else 
		{
			dbesig = g_slice_new0 (DBESignal);
			dbesig->value = GINT_TO_POINTER (DO_UPDATE_SYMS);
			dbesig->process_id = priv->current_scan_process_id;

			/* no */
			g_async_queue_push (priv->scan_aqueue, dbesig);
		}
	}
	else 
	{
		DBESignal *dbesig;
		
		if (partial_count + 1 >= files_list_len) 
		{
			dbesig = g_slice_new0 (DBESignal);
			dbesig->value = GINT_TO_POINTER (DONT_UPDATE_SYMS_AND_EXIT);
			dbesig->process_id = priv->current_scan_process_id;
			
			/* yes */
			g_async_queue_push (priv->scan_aqueue, dbesig);
		}
		else 
		{
			dbesig = g_slice_new0 (DBESignal);
			dbesig->value = GINT_TO_POINTER (DONT_UPDATE_SYMS);
			dbesig->process_id = priv->current_scan_process_id;
			
			/* no */
			g_async_queue_push (priv->scan_aqueue, dbesig);
		}
	}

	/* don't forget to add the real_files if the caller provided a list for
	 * them! */
	if (real_file != NULL)
	{
		DBESignal *dbesig;
		
		dbesig = g_slice_new0 (DBESignal);
		dbesig->value = real_file;
		dbesig->process_id = priv->current_scan_process_id;

		g_async_queue_push (priv->scan_aqueue, dbesig);
	}
	else 
	{
		DBESignal *dbesig;
		
		dbesig = g_slice_new0 (DBESignal);
		dbesig->value = GINT_TO_POINTER (DONT_FAKE_UPDATE_SYMS);
		dbesig->process_id = priv->current_scan_process_id;
		
		/* else add a DONT_FAKE_UPDATE_SYMS marker, just to notify that this 
		 * is not a fake file scan 
		 */
		g_async_queue_push (priv->scan_aqueue, dbesig);
	}	
	
	/* we don't need ginfo object anymore, bye */
	g_object_unref (ginfo);
	g_object_unref (gfile);
	g_free (local_path);
	g_free (sf_data);
	/* no need to free real_file. For two reasons: 1. it's null. 2. it has been
	 * pushed in the async queue and will be freed later
	 */
}

/*
 * sdb_sort_files_list:
 * file1: First file
 * file2: second file
 * 
 * Returns: 
 * -1 if file1 will be sorted before file2
 * 0 if file1 and file2 are sorted equally
 * 1 if file2 will be sorted before file1
 */
static gint sdb_sort_files_list (gconstpointer file1, gconstpointer file2)
{
	const gchar* filename1 = file1;
	const gchar* filename2 = file2;

	if (g_str_has_suffix (filename1, ".h") ||
	    g_str_has_suffix (filename1, ".hxx") ||
	    g_str_has_suffix (filename1, ".hh"))
	{
		return -1;
	}
	else if (g_str_has_suffix (filename2, ".h") ||
	    g_str_has_suffix (filename2, ".hxx") ||
	    g_str_has_suffix (filename2, ".hh"))
	{
		return 1;
	}
	
	return 0;
}
	
	
/* Scan with ctags and produce an output 'tags' file [shared memory file]
 * containing language symbols. This function will call ctags 
 * executale and then sdb_engine_populate_db_by_tags () when it'll detect some
 * output.
 * Please note the files_list/real_files_list parameter:
 * this version of sdb_engine_scan_files_1 () let you scan for text buffer(s) that
 * will be claimed as buffers for the real files.
 * 1. simple mode: files_list represents the real files on disk and so we don't 
 * need real_files_list, which will be NULL.
 * 2. advanced mode: files_list represents temporary flushing of buffers on disk, i.e.
 * /tmp/anjuta_XYZ.cxx. real_files_list is the representation of those files on 
 * database. On the above example we can have anjuta_XYZ.cxx mapped as /src/main.c 
 * on db. In this mode files_list and real_files_list must have the same size.
 *
 */
static gboolean
sdb_engine_scan_files_1 (SymbolDBEngine * dbe, GPtrArray * files_list,
                         GPtrArray *real_files_list, gboolean symbols_update,
                         gint scan_id)
{
	SymbolDBEnginePriv *priv;
	gint i;

	priv = dbe->priv;
	
	/* if ctags_launcher isn't initialized, then do it now. */
	/* lazy initialization */
	if (priv->ctags_launcher == NULL) 
	{
		sdb_engine_ctags_launcher_create (dbe);
	}

	
	/* Enter scanning state */
	priv->is_scanning = TRUE;

	priv->current_scan_process_id = scan_id;
	
	DBESignal *dbesig;

	dbesig = g_slice_new0 (DBESignal);
	dbesig->value = GINT_TO_POINTER (SCAN_BEGIN + 1);
	dbesig->process_id = priv->current_scan_process_id;
	
	g_async_queue_push (priv->signals_aqueue, dbesig);	

#ifdef DEBUG	
	if (priv->first_scan_timer_DEBUG == NULL)
		priv->first_scan_timer_DEBUG = g_timer_new ();
#endif	
	
	/* create the shared memory file */
	if (priv->shared_mem_file == 0)
	{
		gchar *temp_file;
		gint i = 0;
		while (TRUE)
		{
			temp_file = g_strdup_printf ("/anjuta-%d_%ld%d.tags", getpid (),
								 time (NULL), i++);
			gchar *test;
			test = g_strconcat (SHARED_MEMORY_PREFIX, temp_file, NULL);
			if (g_file_test (test, G_FILE_TEST_EXISTS) == TRUE)
			{
				DEBUG_PRINT ("Temp file %s already exists... retrying", test);
				g_free (test);
				g_free (temp_file);
				continue;
			}
			else
			{
				g_free (test);
				break;
			}
		}

		priv->shared_mem_str = temp_file;
		
		if ((priv->shared_mem_fd = 
			 shm_open (temp_file, O_CREAT|O_RDWR, S_IRUSR|S_IWUSR)) < 0)
		{
			g_error ("Error while trying to open a shared memory file. Be"
					   "sure to have "SHARED_MEMORY_PREFIX" mounted with tmpfs");
		}
	
		priv->shared_mem_file = fdopen (priv->shared_mem_fd, "a+b");

		/* no need to free temp_file (alias shared_mem_str). It will be freed on plugin finalize */
	}

	/* Sort the files to have sources before headers */
	g_ptr_array_sort (files_list, sdb_sort_files_list);
	if (real_files_list)
		g_ptr_array_sort (real_files_list, sdb_sort_files_list);
	
	for (i = 0; i < files_list->len; i++)
	{
		GFile *gfile;
		ScanFiles1Data *sf_data;
		gchar *node = (gchar *) g_ptr_array_index (files_list, i);
		gfile = g_file_new_for_path (node);
	
		/* prepare an ojbect where to store some data for the async call */
		sf_data = g_new0 (ScanFiles1Data, 1);
		sf_data->dbe = dbe;
		sf_data->files_list_len = files_list->len;
		sf_data->partial_count = i;
		sf_data->symbols_update = symbols_update;
		
		if (real_files_list != NULL)
		{
			sf_data->real_file = g_strdup (g_ptr_array_index (real_files_list, i));
		}
		else
		{
			sf_data->real_file = NULL;
		}

		/* call it */
		g_file_query_info_async (gfile, 
								 G_FILE_ATTRIBUTE_ACCESS_CAN_READ, 
								 G_FILE_QUERY_INFO_NONE, 
								 G_PRIORITY_LOW,
								 NULL,
								 (GAsyncReadyCallback)sdb_engine_scan_files_2,
								 sf_data);
	}

	return TRUE;
}

static void
on_scan_files_async_end (SymbolDBEngine *dbe, gint process_id, gpointer user_data)
{
	SymbolDBEnginePriv *priv;
	EngineScanDataAsync *esda;

	priv = dbe->priv;
	
	/* fine, check on the queue if there's something left to scan */
	if ((esda = g_async_queue_try_pop (priv->waiting_scan_aqueue)) == NULL)
		return;

	sdb_engine_scan_files_1 (dbe, esda->files_list, esda->real_files_list, 
	    esda->symbols_update, esda->scan_id);

	sdb_engine_scan_data_destroy (esda);	
}

static gboolean
sdb_engine_scan_files_async (SymbolDBEngine * dbe, GPtrArray * files_list,
                             GPtrArray *real_files_list, gboolean symbols_update,
                             gint scan_id)
{
	SymbolDBEnginePriv *priv;
	g_return_val_if_fail (files_list != NULL, FALSE);
	
	if (files_list->len == 0)
		return FALSE;	
	
	priv = dbe->priv;

	if (real_files_list != NULL && (files_list->len != real_files_list->len)) 
	{
		g_warning ("no matched size between real_files_list and files_list");		
		return FALSE;
	}

	/* is the engine scanning or is there already something waiting on the queue? */
	if (symbol_db_engine_is_scanning (dbe) == TRUE ||
	    g_async_queue_length (priv->waiting_scan_aqueue) > 0)
	{
		/* push the data into the queue for later retrieval */
		EngineScanDataAsync * esda = g_new0 (EngineScanDataAsync, 1);

		esda->files_list = anjuta_util_clone_string_gptrarray (files_list);
		if (real_files_list)
			esda->real_files_list = anjuta_util_clone_string_gptrarray (real_files_list);
		else
			esda->real_files_list = NULL;
		esda->symbols_update = symbols_update;
		esda->scan_id = scan_id;

		g_async_queue_push (priv->waiting_scan_aqueue, esda);
		return TRUE;
	}

	/* there's no scan active right now nor data waiting on the queue. 
	 * Proceed with normal scan.
	 */
	sdb_engine_scan_files_1 (dbe, files_list, real_files_list, symbols_update, scan_id);

	return TRUE;
}

static void
sdb_engine_init (SymbolDBEngine * object)
{
	SymbolDBEngine *sdbe;
	GHashTable *h;
	
	sdbe = SYMBOL_DB_ENGINE (object);
	sdbe->priv = g_new0 (SymbolDBEnginePriv, 1);

	sdbe->priv->db_connection = NULL;
	sdbe->priv->sql_parser = NULL;
	sdbe->priv->db_directory = NULL;
	sdbe->priv->project_directory = NULL;
	sdbe->priv->cnc_string = NULL;	
	
	/* initialize an hash table to be used and shared with Iterators */
	sdbe->priv->sym_type_conversion_hash =
		g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);	
	h = sdbe->priv->sym_type_conversion_hash;

	/* please if you change some value below here remember to change also on */
	g_hash_table_insert (h, g_strdup("class"), 
				GINT_TO_POINTER (IANJUTA_SYMBOL_TYPE_CLASS));

	g_hash_table_insert (h, g_strdup("enum"), 
				GINT_TO_POINTER (IANJUTA_SYMBOL_TYPE_ENUM));

	g_hash_table_insert (h, g_strdup("enumerator"), 
				GINT_TO_POINTER (IANJUTA_SYMBOL_TYPE_ENUMERATOR));

	g_hash_table_insert (h, g_strdup("field"), 
				GINT_TO_POINTER (IANJUTA_SYMBOL_TYPE_FIELD));

	g_hash_table_insert (h, g_strdup("function"), 
				GINT_TO_POINTER (IANJUTA_SYMBOL_TYPE_FUNCTION));

	g_hash_table_insert (h, g_strdup("interface"), 
				GINT_TO_POINTER (IANJUTA_SYMBOL_TYPE_INTERFACE));

	g_hash_table_insert (h, g_strdup("member"), 
				GINT_TO_POINTER (IANJUTA_SYMBOL_TYPE_MEMBER));

	g_hash_table_insert (h, g_strdup("method"), 
				GINT_TO_POINTER (IANJUTA_SYMBOL_TYPE_METHOD));

	g_hash_table_insert (h, g_strdup("namespace"), 
				GINT_TO_POINTER (IANJUTA_SYMBOL_TYPE_NAMESPACE));

	g_hash_table_insert (h, g_strdup("package"), 
				GINT_TO_POINTER (IANJUTA_SYMBOL_TYPE_PACKAGE));

	g_hash_table_insert (h, g_strdup("prototype"), 
				GINT_TO_POINTER (IANJUTA_SYMBOL_TYPE_PROTOTYPE));
				
	g_hash_table_insert (h, g_strdup("struct"), 
				GINT_TO_POINTER (IANJUTA_SYMBOL_TYPE_STRUCT));
				
	g_hash_table_insert (h, g_strdup("typedef"), 
				GINT_TO_POINTER (IANJUTA_SYMBOL_TYPE_TYPEDEF));
				
	g_hash_table_insert (h, g_strdup("union"), 
				GINT_TO_POINTER (IANJUTA_SYMBOL_TYPE_UNION));
				
	g_hash_table_insert (h, g_strdup("variable"), 
				GINT_TO_POINTER (IANJUTA_SYMBOL_TYPE_VARIABLE));

	g_hash_table_insert (h, g_strdup("externvar"), 
				GINT_TO_POINTER (IANJUTA_SYMBOL_TYPE_EXTERNVAR));

	g_hash_table_insert (h, g_strdup("macro"), 
				GINT_TO_POINTER (IANJUTA_SYMBOL_TYPE_MACRO));

	g_hash_table_insert (h, g_strdup("macro_with_arg"), 
				GINT_TO_POINTER (IANJUTA_SYMBOL_TYPE_MACRO_WITH_ARG));

	g_hash_table_insert (h, g_strdup("file"), 
				GINT_TO_POINTER (IANJUTA_SYMBOL_TYPE_FILE));

	g_hash_table_insert (h, g_strdup("other"), 
				GINT_TO_POINTER (IANJUTA_SYMBOL_TYPE_OTHER));
	
	
	/* create the hash table that will store shared memory files strings used for 
	 * buffer scanning. Un Engine destroy will unlink () them.
	 */
	sdbe->priv->garbage_shared_mem_files = g_hash_table_new_full (g_str_hash, g_str_equal, 
													  g_free, NULL);	
	
	sdbe->priv->ctags_launcher = NULL;
	sdbe->priv->removed_launchers = NULL;
	sdbe->priv->shutting_down = FALSE;
	sdbe->priv->is_first_population = FALSE;

	sdbe->priv->symbols_scanned_count = 0;

	/* set the ctags executable path to NULL */
	sdbe->priv->ctags_path = NULL;

	/* NULL the name of the default db */
	sdbe->priv->anjuta_db_file = NULL;

	/* identify the scan process with an id. There can be multiple files associated
	 * within a process. A call to scan_files () will put inside the queue an id
	 * returned and emitted by scan-end.
	 */
	sdbe->priv->scan_process_id_sequence = sdbe->priv->current_scan_process_id = 1;
	
	/* the scan_aqueue? It will contain mainly 
	 * ints that refer to the force_update status.
	 */
	sdbe->priv->scan_aqueue = g_async_queue_new ();

	/* the thread pool for tags scannning */
	sdbe->priv->thread_pool = g_thread_pool_new (sdb_engine_ctags_output_thread,
												 sdbe, THREADS_MAX_CONCURRENT,
												 FALSE, NULL);

	/* initialize the sdb mutex */
	g_mutex_init (&sdbe->priv->mutex);

	/* some signals queues */
	sdbe->priv->signals_aqueue = g_async_queue_new ();
	sdbe->priv->updated_syms_id_aqueue = g_async_queue_new ();
	sdbe->priv->updated_scope_syms_id_aqueue = g_async_queue_new ();
	sdbe->priv->inserted_syms_id_aqueue = g_async_queue_new ();
	sdbe->priv->is_scanning = FALSE;

	sdbe->priv->waiting_scan_aqueue = g_async_queue_new_full (sdb_engine_scan_data_destroy);
	sdbe->priv->waiting_scan_handler = g_signal_connect (G_OBJECT (sdbe), "scan-end",
 				G_CALLBACK (on_scan_files_async_end), NULL);

	
	/*
	 * STATIC QUERY STRUCTURE INITIALIZE
	 */
	
	/* -- workspace -- */
	STATIC_QUERY_POPULATE_INIT_NODE(sdbe->priv->static_query_list, 
									PREP_QUERY_WORKSPACE_NEW, 
		"INSERT INTO workspace (workspace_name, analyse_time) VALUES (\
			## /* name:'wsname' type:gchararray */, \
	 		datetime ('now', 'localtime'))");
	
	STATIC_QUERY_POPULATE_INIT_NODE(sdbe->priv->static_query_list, 
									PREP_QUERY_GET_WORKSPACE_ID_BY_UNIQUE_NAME, 
	 	"SELECT workspace_id FROM workspace \
	     WHERE \
	    	workspace_name = ## /* name:'wsname' type:gchararray */ LIMIT 1");

	/* -- project -- */
	STATIC_QUERY_POPULATE_INIT_NODE(sdbe->priv->static_query_list, 
									PREP_QUERY_PROJECT_NEW, 
	 	"INSERT INTO project (project_name, project_version, wrkspace_id, analyse_time) VALUES (\
		 	## /* name:'prjname' type:gchararray */, \
			## /* name:'prjversion' type:gchararray */, \
	 		(SELECT workspace_id FROM workspace \
	    	 WHERE \
	    			workspace_name = ## /* name:'wsname' type:gchararray */ LIMIT 1), \
	    	datetime ('now', 'localtime'))");

	STATIC_QUERY_POPULATE_INIT_NODE(sdbe->priv->static_query_list, 
									PREP_QUERY_GET_PROJECT_ID_BY_UNIQUE_NAME, 
	 	"SELECT project_id FROM project \
	     WHERE \
	    	project_name = ## /* name:'prjname' type:gchararray */ AND \
	    	project_version = ## /* name:'prjversion' type:gchararray */ \
	     LIMIT 1");

	STATIC_QUERY_POPULATE_INIT_NODE(sdbe->priv->static_query_list, 
									PREP_QUERY_UPDATE_PROJECT_ANALYSE_TIME, 
	 	"UPDATE project SET \
	    	analyse_time = datetime('now', 'localtime', '+10 seconds') \
	     WHERE \
	 		project_name = ## /* name:'prjname' type:gchararray */");
	
	/* -- file -- */
	STATIC_QUERY_POPULATE_INIT_NODE(sdbe->priv->static_query_list, 
									PREP_QUERY_FILE_NEW, 
	 	"INSERT INTO file (file_path, prj_id, lang_id, analyse_time) VALUES (\
	 	  ## /* name:'filepath' type:gchararray */, \
		  (SELECT project_id FROM project \
	     	WHERE \
	    		project_name = ## /* name:'prjname' type:gchararray */ AND \
	    		project_version = ## /* name:'prjversion' type:gchararray */ \
	    		LIMIT 1), \
		  ## /* name:'langid' type:gint */, \
	 	  datetime ('now', 'localtime'))");
	
	STATIC_QUERY_POPULATE_INIT_NODE(sdbe->priv->static_query_list, 
									PREP_QUERY_GET_FILE_ID_BY_UNIQUE_NAME, 
		"SELECT file_id FROM file \
	     WHERE \
	    	file_path = ## /* name:'filepath' type:gchararray */ LIMIT 1");
	
	STATIC_QUERY_POPULATE_INIT_NODE(sdbe->priv->static_query_list, 
									PREP_QUERY_GET_ALL_FROM_FILE_BY_PROJECT_NAME,
		"SELECT file_id, file_path AS db_file_path, prj_id, lang_id, file.analyse_time \
		 FROM file JOIN project ON project.project_id = file.prj_id \
	     WHERE \
		 	project.project_name = ## /* name:'prjname' type:gchararray */");

	STATIC_QUERY_POPULATE_INIT_NODE(sdbe->priv->static_query_list, 
									PREP_QUERY_UPDATE_FILE_ANALYSE_TIME,
		"UPDATE file SET \
	    	analyse_time = datetime('now', 'localtime') \
	     WHERE \
	 	 	file_path = ## /* name:'filepath' type:gchararray */");

	STATIC_QUERY_POPULATE_INIT_NODE(sdbe->priv->static_query_list, 
									PREP_QUERY_GET_ALL_FROM_FILE_WHERE_NOT_IN_SYMBOLS,
		"SELECT file_id, file_path AS db_file_path FROM file \
	     WHERE file_id NOT IN (SELECT file_defined_id FROM symbol)");
	
	/* -- language -- */
	STATIC_QUERY_POPULATE_INIT_NODE(sdbe->priv->static_query_list, 
									PREP_QUERY_LANGUAGE_NEW,
		"INSERT INTO language (language_name) VALUES (\
			## /* name:'langname' type:gchararray */)");

	STATIC_QUERY_POPULATE_INIT_NODE(sdbe->priv->static_query_list, 
									PREP_QUERY_GET_LANGUAGE_ID_BY_UNIQUE_NAME,
	 	"SELECT language_id FROM language WHERE \
	    	language_name = ## /* name:'langname' type:gchararray */ LIMIT 1");

	/* -- sym kind -- */
	STATIC_QUERY_POPULATE_INIT_NODE(sdbe->priv->static_query_list, 
									PREP_QUERY_SYM_KIND_NEW,
	 	"INSERT INTO sym_kind (kind_name, is_container) VALUES(\
		 	## /* name:'kindname' type:gchararray */, \
			## /* name:'container' type:gint */)");

	STATIC_QUERY_POPULATE_INIT_NODE(sdbe->priv->static_query_list, 
									PREP_QUERY_GET_SYM_KIND_BY_UNIQUE_NAME,
		"SELECT sym_kind_id FROM sym_kind WHERE \
	    	kind_name = ## /* name:'kindname' type:gchararray */");
	
	/* -- sym access -- */
	STATIC_QUERY_POPULATE_INIT_NODE(sdbe->priv->static_query_list, 
									PREP_QUERY_SYM_ACCESS_NEW,
		"INSERT INTO sym_access (access_name) VALUES(\
			## /* name:'accesskind' type:gchararray */)");

	STATIC_QUERY_POPULATE_INIT_NODE(sdbe->priv->static_query_list, 
									PREP_QUERY_GET_SYM_ACCESS_BY_UNIQUE_NAME,
		"SELECT access_kind_id FROM sym_access WHERE \
	    	access_name = ## /* name:'accesskind' type:gchararray */ LIMIT 1");
	
	/* -- sym implementation -- */
	STATIC_QUERY_POPULATE_INIT_NODE(sdbe->priv->static_query_list, 
		 							PREP_QUERY_SYM_IMPLEMENTATION_NEW,
	 	"INSERT INTO sym_implementation (implementation_name) VALUES(\
		 	## /* name:'implekind' type:gchararray */)");
	
	STATIC_QUERY_POPULATE_INIT_NODE(sdbe->priv->static_query_list, 
	 								PREP_QUERY_GET_SYM_IMPLEMENTATION_BY_UNIQUE_NAME,
	 	"SELECT sym_impl_id FROM sym_implementation WHERE \
	    	kind = ## /* name:'implekind' type:gchararray */ LIMIT 1");
	
	/* -- heritage -- */
	STATIC_QUERY_POPULATE_INIT_NODE(sdbe->priv->static_query_list, 
	 								PREP_QUERY_HERITAGE_NEW,
	 	"INSERT INTO heritage (symbol_id_base, symbol_id_derived) VALUES(\
		 	## /* name:'symbase' type:gint */, \
			## /* name:'symderived' type:gint */)");	
	
	/* -- scope -- */
	STATIC_QUERY_POPULATE_INIT_NODE(sdbe->priv->static_query_list, 
	 								PREP_QUERY_SCOPE_NEW,
	 	"INSERT INTO scope (scope_name) VALUES(\
		 		## /* name:'scope' type:gchararray */)");
	
	STATIC_QUERY_POPULATE_INIT_NODE(sdbe->priv->static_query_list, 
	 								PREP_QUERY_GET_SCOPE_ID,
	 	"SELECT scope_id FROM scope \
	     WHERE scope_name = ## /* name:'scope' type:gchararray */ \
	     LIMIT 1");
	
	/* -- symbol -- */
	STATIC_QUERY_POPULATE_INIT_NODE(sdbe->priv->static_query_list, 
	 								PREP_QUERY_SYMBOL_NEW,
	 	"INSERT INTO symbol (file_defined_id, name, file_position, \
	 	 					 is_file_scope, signature, returntype, \
	    					 scope_definition_id, scope_id, \
	    					 type_type, type_name, kind_id, access_kind_id, \
	 						 implementation_kind_id, update_flag) \
	    		VALUES( \
	 				## /* name:'filedefid' type:gint */, \
					## /* name:'name' type:gchararray */, \
					## /* name:'fileposition' type:gint */, \
					## /* name:'isfilescope' type:gint */, \
					## /* name:'signature' type:gchararray */, \
					## /* name:'returntype' type:gchararray */, \
	    			## /* name:'scopedefinitionid' type:gint */, \
					## /* name:'scopeid' type:gint */, \
					## /* name:'typetype' type:gchararray */, \
	    			## /* name:'typename' type:gchararray */, \
					## /* name:'kindid' type:gint */,## /* name:'accesskindid' type:gint */, \
					## /* name:'implementationkindid' type:gint */, \
					## /* name:'updateflag' type:gint */)");
	

	STATIC_QUERY_POPULATE_INIT_NODE(sdbe->priv->static_query_list, 
	 								PREP_QUERY_GET_SYMBOL_ID_BY_CLASS_NAME,
	 	"SELECT symbol_id FROM symbol \
	 	 WHERE scope_id = 0 AND \
	    	   type_type='class' AND \
	 	  	   name = ## /* name:'klassname' type:gchararray */ LIMIT 1");
	
	STATIC_QUERY_POPULATE_INIT_NODE(sdbe->priv->static_query_list, 
	 								PREP_QUERY_GET_SYMBOL_ID_BY_CLASS_NAME_AND_NAMESPACE,
	 	"SELECT symbol_id FROM symbol JOIN scope ON symbol.scope_id = scope.scope_id \
	 	 WHERE symbol.name = ## /* name:'klassname' type:gchararray */ AND \
	 	 	   scope.scope_name = ## /* name:'namespacename' type:gchararray */ AND \
	 	 	   symbol.type_type = 'namespace' LIMIT 1");
	
	STATIC_QUERY_POPULATE_INIT_NODE(sdbe->priv->static_query_list, 
	 								PREP_QUERY_UPDATE_SYMBOL_SCOPE_ID,
	 	"UPDATE OR IGNORE symbol SET scope_id = (SELECT scope_definition_id FROM symbol WHERE \
	    	type_type = ## /* name:'tokenname' type:gchararray */ AND \
	    	type_name = ## /* name:'objectname' type:gchararray */ LIMIT 1) \
	 	 WHERE symbol_id = ## /* name:'symbolid' type:gint */");
	
	STATIC_QUERY_POPULATE_INIT_NODE(sdbe->priv->static_query_list, 
	 								PREP_QUERY_GET_SYMBOL_ID_BY_UNIQUE_INDEX_KEY_EXT,
	 	"SELECT symbol_id FROM symbol \
	     WHERE \
	    	name = ## /* name:'symname' type:gchararray */ AND \
	    	file_defined_id =  ## /* name:'filedefid' type:gint */ AND \
	    	type_type = ## /* name:'typetype' type:gchararray */ AND \
	    	type_name = ## /* name:'typename' type:gchararray */ AND \
	    	update_flag = 0 \
		  ORDER BY abs(file_position - ## /* name:'fileposition' type:gint */) \
		  LIMIT 1");
	
	STATIC_QUERY_POPULATE_INIT_NODE(sdbe->priv->static_query_list, 
	 								PREP_QUERY_UPDATE_SYMBOL_ALL,
	 	"UPDATE symbol SET \
	    		is_file_scope = ## /* name:'isfilescope' type:gint */, \
	    		file_position = ## /* name:'fileposition' type:gint */, \
	    		signature = ## /* name:'signature' type:gchararray */, \
	    		returntype = ## /* name:'returntype' type:gchararray */, \
	    		scope_definition_id = ## /* name:'scopedefinitionid' type:gint */, \
	    		scope_id = ## /* name:'scopeid' type:gint */, \
	    		kind_id = ## /* name:'kindid' type:gint */, \
	    		access_kind_id = ## /* name:'accesskindid' type:gint */, \
	    		implementation_kind_id = ## /* name:'implementationkindid' type:gint */, \
	    		update_flag = ## /* name:'updateflag' type:gint */ \
	    WHERE symbol_id = ## /* name:'symbolid' type:gint */");
	
	STATIC_QUERY_POPULATE_INIT_NODE(sdbe->priv->static_query_list, 
	 								PREP_QUERY_REMOVE_NON_UPDATED_SYMBOLS,
	 	"DELETE FROM symbol WHERE \
	    	file_defined_id = (SELECT file_id FROM file \
	    						WHERE \
	    							file.file_path = ## /* name:'filepath' type:gchararray */) AND \
	 	 	update_flag = 0");
	
	STATIC_QUERY_POPULATE_INIT_NODE(sdbe->priv->static_query_list, 
	 								PREP_QUERY_RESET_UPDATE_FLAG_SYMBOLS,
	 	"UPDATE symbol SET \
	    	update_flag = 0 \
	 	 WHERE file_defined_id = (SELECT file_id FROM file \
	    						  WHERE \
	 	 							file_path = ## /* name:'filepath' type:gchararray */)");
	
	/* -- tmp_removed -- */
	STATIC_QUERY_POPULATE_INIT_NODE(sdbe->priv->static_query_list, 
	 								PREP_QUERY_GET_REMOVED_IDS,
	 	"SELECT symbol_removed_id FROM __tmp_removed");
	
	STATIC_QUERY_POPULATE_INIT_NODE(sdbe->priv->static_query_list, 
	 								PREP_QUERY_TMP_REMOVED_DELETE_ALL,
	 	"DELETE FROM __tmp_removed");

	STATIC_QUERY_POPULATE_INIT_NODE(sdbe->priv->static_query_list, 
	 								PREP_QUERY_REMOVE_FILE_BY_PROJECT_NAME,
	 	"DELETE FROM file WHERE \
	    	prj_id = (SELECT project_id FROM project \
	    			  WHERE project_name = ## /* name:'prjname' type:gchararray */) AND \
	    	file_path = ## /* name:'filepath' type:gchararray */");
	
	/* init cache hashtables */
	sdb_engine_init_caches (sdbe);

	/* init table maps */
	sdb_engine_init_table_maps (sdbe);
}

static void
sdb_engine_unlink_shared_files (gpointer key, gpointer value, gpointer user_data)
{
	shm_unlink (key);
}

static void 
sdb_engine_unref_removed_launchers (gpointer data, gpointer user_data)
{
	g_object_unref (data);
}

static void
sdb_engine_finalize (GObject * object)
{
	SymbolDBEngine *dbe;
	SymbolDBEnginePriv *priv;
	
	dbe = SYMBOL_DB_ENGINE (object);
	priv = dbe->priv;
/*/ FIXME a crash here ?!
	g_signal_handler_disconnect (dbe, priv->waiting_scan_handler);
	priv->waiting_scan_handler = 0;
//*/
	if (priv->thread_pool)
	{
		g_thread_pool_free (priv->thread_pool, TRUE, TRUE);
		priv->thread_pool = NULL;
	}
	
	if (priv->ctags_launcher)
	{
		g_object_unref (priv->ctags_launcher);
		priv->ctags_launcher = NULL;
	}		
	
	if (priv->removed_launchers)
	{
		g_list_foreach (priv->removed_launchers, 
						sdb_engine_unref_removed_launchers, NULL);
		g_list_free (priv->removed_launchers);
		priv->removed_launchers = NULL;
	}

	g_mutex_clear (&priv->mutex);

	if (priv->timeout_trigger_handler > 0)
		g_source_remove (priv->timeout_trigger_handler);

	if (symbol_db_engine_is_connected (dbe) == TRUE)
		sdb_engine_disconnect_from_db (dbe);	
	
	sdb_engine_free_cached_queries (dbe);
	
	if (priv->scan_aqueue)
	{
		g_async_queue_unref (priv->scan_aqueue);
		priv->scan_aqueue = NULL;
	}
	
	if (priv->updated_syms_id_aqueue)
	{
		g_async_queue_unref (priv->updated_syms_id_aqueue);
		priv->updated_syms_id_aqueue = NULL;
	}	

	if (priv->updated_scope_syms_id_aqueue)
	{
		g_async_queue_unref (priv->updated_scope_syms_id_aqueue);
		priv->updated_scope_syms_id_aqueue = NULL;
	}	
	
	if (priv->inserted_syms_id_aqueue)
	{
		g_async_queue_unref (priv->inserted_syms_id_aqueue);
		priv->inserted_syms_id_aqueue = NULL;
	}	

	if (priv->waiting_scan_aqueue)
	{
		g_async_queue_unref (priv->waiting_scan_aqueue);
		priv->waiting_scan_aqueue = NULL;
	}
	
	if (priv->shared_mem_file) 
	{
		fclose (priv->shared_mem_file);
		priv->shared_mem_file = NULL; 
	}	
	
	if (priv->shared_mem_str)
	{
		shm_unlink (priv->shared_mem_str);
		g_free (priv->shared_mem_str);
		priv->shared_mem_str = NULL;
	}
	
	if (priv->garbage_shared_mem_files)
	{
		g_hash_table_foreach (priv->garbage_shared_mem_files, 
							  sdb_engine_unlink_shared_files,
							  NULL);
		/* destroy the hash table */
		g_hash_table_destroy (priv->garbage_shared_mem_files);
	}
	

	if (priv->sym_type_conversion_hash)
		g_hash_table_destroy (priv->sym_type_conversion_hash);
	priv->sym_type_conversion_hash = NULL;
	
	if (priv->signals_aqueue)
		g_async_queue_unref (priv->signals_aqueue);
	priv->signals_aqueue = NULL;
	
	sdb_engine_clear_caches (dbe);
	sdb_engine_clear_tablemaps (dbe);

	g_free (priv->anjuta_db_file);
	priv->anjuta_db_file = NULL;
	
	g_free (priv->ctags_path);
	priv->ctags_path = NULL;
	
	g_free (priv);
	
	G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
sdb_engine_class_init (SymbolDBEngineClass * klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	parent_class = G_OBJECT_CLASS (g_type_class_peek_parent (klass));

	object_class->finalize = sdb_engine_finalize;

	signals[DB_CONNECTED]
		= g_signal_new ("db-connected",
						G_OBJECT_CLASS_TYPE (object_class),
						G_SIGNAL_RUN_LAST,
						G_STRUCT_OFFSET (SymbolDBEngineClass, db_connected),
						NULL, NULL,
						g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 0);
	
	signals[DB_DISCONNECTED]
		= g_signal_new ("db-disconnected",
						G_OBJECT_CLASS_TYPE (object_class),
						G_SIGNAL_RUN_LAST,
						G_STRUCT_OFFSET (SymbolDBEngineClass, db_disconnected),
						NULL, NULL,
						g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 0);
	
	signals[SCAN_BEGIN]
		= g_signal_new ("scan-begin",
						G_OBJECT_CLASS_TYPE (object_class),
						G_SIGNAL_RUN_LAST,
						G_STRUCT_OFFSET (SymbolDBEngineClass, scan_begin),
						NULL, NULL,
						g_cclosure_marshal_VOID__INT, G_TYPE_NONE, 
						1,
						G_TYPE_INT);

	signals[SINGLE_FILE_SCAN_END]
		= g_signal_new ("single-file-scan-end",
						G_OBJECT_CLASS_TYPE (object_class),
						G_SIGNAL_RUN_FIRST,
						G_STRUCT_OFFSET (SymbolDBEngineClass, single_file_scan_end),
						NULL, NULL,
						g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 0);
	
	signals[SCAN_END]
		= g_signal_new ("scan-end",
						G_OBJECT_CLASS_TYPE (object_class),
						G_SIGNAL_RUN_LAST,
						G_STRUCT_OFFSET (SymbolDBEngineClass, scan_end),
						NULL, NULL,
						g_cclosure_marshal_VOID__INT, G_TYPE_NONE, 
						1,
						G_TYPE_INT);

	signals[SYMBOL_INSERTED]
		= g_signal_new ("symbol-inserted",
						G_OBJECT_CLASS_TYPE (object_class),
						G_SIGNAL_RUN_LAST,
						G_STRUCT_OFFSET (SymbolDBEngineClass, symbol_inserted),
						NULL, NULL,
						g_cclosure_marshal_VOID__INT, G_TYPE_NONE, 
						1,
						G_TYPE_INT);
	
	signals[SYMBOL_UPDATED]
		= g_signal_new ("symbol-updated",
						G_OBJECT_CLASS_TYPE (object_class),
						G_SIGNAL_RUN_LAST,
						G_STRUCT_OFFSET (SymbolDBEngineClass, symbol_updated),
						NULL, NULL,
						g_cclosure_marshal_VOID__INT, G_TYPE_NONE, 
						1,
						G_TYPE_INT);

	signals[SYMBOL_SCOPE_UPDATED]
		= g_signal_new ("symbol-scope-updated",
						G_OBJECT_CLASS_TYPE (object_class),
						G_SIGNAL_RUN_LAST,
						G_STRUCT_OFFSET (SymbolDBEngineClass, symbol_scope_updated),
						NULL, NULL,
						g_cclosure_marshal_VOID__INT, G_TYPE_NONE, 
						1,
						G_TYPE_INT);
	
	signals[SYMBOL_REMOVED]
		= g_signal_new ("symbol-removed",
						G_OBJECT_CLASS_TYPE (object_class),
						G_SIGNAL_RUN_LAST,
						G_STRUCT_OFFSET (SymbolDBEngineClass, symbol_removed),
						NULL, NULL,
						g_cclosure_marshal_VOID__INT, G_TYPE_NONE, 
						1,
						G_TYPE_INT);	
}   

GType
sdb_engine_get_type (void)
{
	static GType our_type = 0;

	if (our_type == 0)
	{
		static const GTypeInfo our_info = {
			sizeof (SymbolDBEngineClass),	/* class_size */
			(GBaseInitFunc) NULL,	/* base_init */
			(GBaseFinalizeFunc) NULL,	/* base_finalize */
			(GClassInitFunc) sdb_engine_class_init,	/* class_init */
			(GClassFinalizeFunc) NULL,	/* class_finalize */
			NULL /* class_data */ ,
			sizeof (SymbolDBEngine),	/* instance_size */
			0,					/* n_preallocs */
			(GInstanceInitFunc) sdb_engine_init,	/* instance_init */
			NULL				/* value_table */
		};

		our_type = g_type_register_static (G_TYPE_OBJECT, "SymbolDBEngine",
										   &our_info, 0);
	}

	return our_type;
}

/**
 * symbol_db_engine_set_ctags_path:
 * @dbe: self
 * @ctags_path: Anjuta-tags executable. It is mandatory. No NULL value is accepted.
 * 
 * Set a new path for anjuta-tags executable.
 *
 * Returns: TRUE if the set is successful.
 */ 
gboolean
symbol_db_engine_set_ctags_path (SymbolDBEngine * dbe, const gchar * ctags_path)
{
	SymbolDBEnginePriv *priv;

	g_return_val_if_fail (dbe != NULL, FALSE);
	g_return_val_if_fail (ctags_path != NULL, FALSE);
	
	priv = dbe->priv;
	
	/* Check if ctags is really installed */
	if (!anjuta_util_prog_is_installed (ctags_path, TRUE))
	{
		g_warning ("symbol_db_engine_set_ctags_path (): Wrong path for ctags. Keeping "
				   "the old value %s", priv->ctags_path);
		return priv->ctags_path != NULL;
	}	
	
	/* have we already got it? */
	if (priv->ctags_path != NULL && 
		g_strcmp0 (priv->ctags_path, ctags_path) == 0)
		return TRUE;

	/* free the old value */
	g_free (priv->ctags_path);
	
	/* is anjutalauncher already created? */
	if (priv->ctags_launcher != NULL)
	{
		AnjutaLauncher *tmp;
		tmp = priv->ctags_launcher;

		/* recreate it on the fly */
		sdb_engine_ctags_launcher_create (dbe);

		/* keep the launcher alive to avoid crashes */
		priv->removed_launchers = g_list_prepend (priv->removed_launchers, tmp);
	}	
	
	/* set the new one */
	priv->ctags_path = g_strdup (ctags_path);	
	return TRUE;
}

/**
 * symbol_db_engine_new: 
 * @ctags_path Anjuta-tags executable. It is mandatory. No NULL value is accepted.
 * 
 * Create a new instance of an engine. 
 * Default name of database is ANJUTA_DB_FILE (see symbol-db-engine-priv.h)
 *
 * Returns: a new SymbolDBEngine object.
 */
SymbolDBEngine *
symbol_db_engine_new (const gchar * ctags_path)
{
	SymbolDBEngine *sdbe;
	SymbolDBEnginePriv *priv;
	
	g_return_val_if_fail (ctags_path != NULL, NULL);
	sdbe = g_object_new (SYMBOL_TYPE_DB_ENGINE, NULL);
	
	priv = sdbe->priv;
	priv->anjuta_db_file = g_strdup (ANJUTA_DB_FILE);

	/* set the mandatory ctags_path */
	if (!symbol_db_engine_set_ctags_path (sdbe, ctags_path))
	{
		return NULL;
	}
		
	return sdbe;
}

/**
 * symbol_db_engine_new_full:
 * @ctags_path: Anjuta-tags executable. It is mandatory. No NULL value is accepted.
 * @database_name: name of resulting db on disk.
 * 
 * Similar to symbol_db_engine_new() but you can specify the name of resulting db.
 */
SymbolDBEngine* 
symbol_db_engine_new_full (const gchar * ctags_path, const gchar * database_name)
{
	SymbolDBEngine* dbe;
	SymbolDBEnginePriv* priv;

	g_return_val_if_fail (database_name != NULL, NULL);
	dbe = symbol_db_engine_new (ctags_path);

	g_return_val_if_fail (dbe != NULL, NULL);

	priv = dbe->priv;
	g_free (priv->anjuta_db_file);
	priv->anjuta_db_file = g_strdup (database_name);

	return dbe;
}

/**
 * Set some default parameters to use with the current database.
 */
static void
sdb_engine_set_defaults_db_parameters (SymbolDBEngine * dbe)
{	
	sdb_engine_execute_unknown_sql (dbe, "PRAGMA page_size = 32768");
	sdb_engine_execute_unknown_sql (dbe, "PRAGMA cache_size = 12288");
	sdb_engine_execute_unknown_sql (dbe, "PRAGMA synchronous = OFF");
	sdb_engine_execute_unknown_sql (dbe, "PRAGMA temp_store = MEMORY");	
	sdb_engine_execute_unknown_sql (dbe, "PRAGMA journal_mode = OFF");
	sdb_engine_execute_unknown_sql (dbe, "PRAGMA read_uncommitted = 1");
	sdb_engine_execute_unknown_sql (dbe, "PRAGMA foreign_keys = OFF");
	symbol_db_engine_set_db_case_sensitive (dbe, TRUE);
}

/* Will create priv->db_connection.
 * Connect to database identified by db_directory.
 * Usually db_directory is defined also into priv. We let it here as parameter 
 * because it is required and cannot be null.
 */
static gboolean
sdb_engine_connect_to_db (SymbolDBEngine * dbe, const gchar *cnc_string, GError **error)
{
	SymbolDBEnginePriv *priv;

	g_return_val_if_fail (dbe != NULL, FALSE);
	priv = dbe->priv;	
	
	if (priv->db_connection != NULL)
	{
		/* if it's the case that the connection isn't NULL, we 
		 * should notify the user
		 * and return FALSE. It's his task to disconnect and retry to connect */
		g_warning ("connection is already established. Please disconnect "
				   "and then try to reconnect.");
		return FALSE;
	}

	/* establish a connection. If the sqlite file does not exist it will 
	 * be created 
	 */
	priv->db_connection = gda_connection_open_from_string ("SQLite", cnc_string, NULL,
#ifdef HAVE_GDA6
											GDA_CONNECTION_OPTIONS_NONE,
#else
											GDA_CONNECTION_OPTIONS_THREAD_SAFE,
#endif										
											error);
										
	if (!GDA_IS_CONNECTION (priv->db_connection))
	{
		g_warning ("Could not open connection to %s\n", cnc_string);
		return FALSE;
	}

	priv->cnc_string = g_strdup (cnc_string);
	priv->sql_parser = gda_connection_create_parser (priv->db_connection);
	
	if (!GDA_IS_SQL_PARSER (priv->sql_parser))
	{
		g_set_error_literal (error, SYMBOL_DB_ENGINE_ERROR,
		                     SYMBOL_DB_ENGINE_ERROR_INVALID_PARSER,
		                     _("Could not create sql parser. Check your libgda installation"));
		return FALSE;
	}
	
	DEBUG_PRINT ("Connected to database %s", cnc_string);
	return TRUE;
}

/**
 * symbol_db_engine_is_connected:
 * @dbe: self
 * 
 * Check whether the engine is connected to db or not.
 * 
 * Returns: TRUE if the db is connected.
 */
gboolean
symbol_db_engine_is_connected (SymbolDBEngine * dbe)
{
	SymbolDBEnginePriv *priv;

	g_return_val_if_fail (dbe != NULL, FALSE);
	priv = dbe->priv;	
	
	return priv->db_connection && priv->cnc_string && priv->sql_parser && 
		gda_connection_is_opened (priv->db_connection );
}

/**
 * symbol_db_engine_is_scanning:
 * @dbe: self
 * 
 * Check if engine is scanning busy
 * 
 * Returns: TRUE if it is scanning.
 */
gboolean
symbol_db_engine_is_scanning (SymbolDBEngine *dbe)
{
	g_return_val_if_fail (SYMBOL_IS_DB_ENGINE (dbe), FALSE);
	return dbe->priv->is_scanning;
}

/**
 * Creates required tables for the database to work.
 * Sets is_first_population flag to TRUE.
 * @param tables_sql_file File containing sql code.
 */
static gboolean
sdb_engine_create_db_tables (SymbolDBEngine * dbe, const gchar * tables_sql_file)
{	
	SymbolDBEnginePriv *priv;
	gchar *contents;
	gchar *query;
	gsize sizez;

	g_return_val_if_fail (tables_sql_file != NULL, FALSE);

	priv = dbe->priv;

	g_return_val_if_fail (priv->db_connection != NULL, FALSE);

	/* read the contents of the file */
	if (g_file_get_contents (tables_sql_file, &contents, &sizez, NULL) == FALSE)
	{
		g_warning ("Something went wrong while trying to read %s",
				   tables_sql_file);

		return FALSE;
	}

	sdb_engine_execute_non_select_sql (dbe, contents);	
	g_free (contents);
	
	/* set the current symbol db database version. This may help if new tables/fields
	 * are added/removed in future versions.
	 */
	query = "INSERT INTO version VALUES ("SYMBOL_DB_VERSION")";
	sdb_engine_execute_non_select_sql (dbe, query);	

	priv->is_first_population = TRUE;
	
	/* no need to free query of course */
	
	return TRUE;
}

/**
 * symbol_db_engine_db_exists:
 * @dbe: self
 * @prj_directory: absolute path of the project.
 * 
 * Check if the database already exists into the prj_directory
 * 
 * Returns: TRUE if db exists on disk.
 */
gboolean
symbol_db_engine_db_exists (SymbolDBEngine * dbe, const gchar * prj_directory)
{
	SymbolDBEnginePriv *priv;

	g_return_val_if_fail (prj_directory != NULL, FALSE);

	priv = dbe->priv;

	/* check whether the db filename already exists.*/
	gchar *tmp_file = g_strdup_printf ("%s/%s.db", prj_directory,
									   priv->anjuta_db_file);
	
	if (g_file_test (tmp_file, G_FILE_TEST_EXISTS) == FALSE)
	{
		DEBUG_PRINT ("db %s does not exist", tmp_file);
		g_free (tmp_file);
		return FALSE;
	}

	g_free (tmp_file);
	return TRUE;
}

/**
 * symbol_db_engine_file_exists:
 * @dbe: self
 * @abs_file_path: absolute file path.
 * 
 * Check if a file is already present [and scanned] in db.
 * ~~~ Thread note: this function locks the mutex ~~~
 *
 * Returns: TRUE if the file is present.
 */
gboolean
symbol_db_engine_file_exists (SymbolDBEngine * dbe, const gchar * abs_file_path)
{
	SymbolDBEnginePriv *priv;
	const gchar *relative;
	gint file_defined_id;
	GValue v = {0};

	g_return_val_if_fail (dbe != NULL, FALSE);
	g_return_val_if_fail (abs_file_path != NULL, FALSE);
	
	priv = dbe->priv;

	SDB_LOCK(priv);
	
	relative = symbol_db_util_get_file_db_path (dbe, abs_file_path);
	if (relative == NULL)
	{
		SDB_UNLOCK(priv);
		return FALSE;
	}	

	SDB_GVALUE_SET_STATIC_STRING(v, relative);	

	if ((file_defined_id = sdb_engine_get_tuple_id_by_unique_name (dbe,
													PREP_QUERY_GET_FILE_ID_BY_UNIQUE_NAME,
													"filepath",
													&v)) < 0)
	{	
		SDB_UNLOCK(priv);
		return FALSE;	
	}

	SDB_UNLOCK(priv);
	return TRUE;
}

/** 
 * symbol_db_engine_close_db:
 * @dbe: self
 * 
 * Disconnect db, gda client and db_connection 
 *
 * Returns: TRUE if closing has been successful.
 */
gboolean 
symbol_db_engine_close_db (SymbolDBEngine *dbe)
{
	SymbolDBEnginePriv *priv;
	gboolean ret;
	g_return_val_if_fail (dbe != NULL, FALSE);
	
	priv = dbe->priv;

	/* terminate threads, if ever they're running... */
	g_thread_pool_free (priv->thread_pool, TRUE, TRUE);
	priv->thread_pool = NULL;
	ret = sdb_engine_disconnect_from_db (dbe);

	/* reset count */
	priv->symbols_scanned_count = 0;
	
	g_free (priv->db_directory);
	priv->db_directory = NULL;

	g_free (priv->project_directory);
	priv->project_directory = NULL;	
	
	priv->thread_pool = g_thread_pool_new (sdb_engine_ctags_output_thread,
										   dbe, THREADS_MAX_CONCURRENT,
										   FALSE, NULL);
	g_signal_emit_by_name (dbe, "db-disconnected", NULL);
	return ret;
}

static gdouble 
sdb_engine_get_db_version (SymbolDBEngine *dbe)
{
	GdaDataModel *data_model;
	const GValue *value_id;
	gchar *query;
	gdouble version_id;
	gint col;
	
	/* set the current symbol db database version. This may help if new tables/fields
	 * are added/removed in future versions.
	 */
	query = "SELECT sdb_version FROM version";
	if ((data_model = sdb_engine_execute_select_sql (dbe, query)) == NULL)
	{
		return -1;
	}

	col = gda_data_model_get_column_index(data_model, "sdb_version");
	value_id = gda_data_model_get_value_at (data_model, col, 0, NULL);

	if (G_VALUE_HOLDS_DOUBLE (value_id))
		version_id = g_value_get_double (value_id);
	else
		version_id = (gdouble)g_value_get_int (value_id);
	
	g_object_unref (data_model);
	/* no need to free query of course */
	
	return version_id;
}

static gboolean
sdb_engine_check_db_version_and_upgrade (SymbolDBEngine *dbe, 
                                         const gchar* db_file,
                                         const gchar* cnc_string)
{
	gdouble version;

	
	version = sdb_engine_get_db_version (dbe);
	DEBUG_PRINT ("Checking db version...");
	if (version <= 0) 
	{
		/* some error occurred */
		g_warning ("No version of db detected. This can produce many errors. DB"
		           "will be recreated from scratch.");

		/* force version to 0 */
		version = 0;
	}
		
	if (version < atof (SYMBOL_DB_VERSION))
	{
		DEBUG_PRINT	 ("Upgrading from version %f to "SYMBOL_DB_VERSION, version);
		
		/* we need a full recreation of db. Because of the sym_kind table
		 * which changed its data but not its fields, we must recreate the
		 * whole database.
		 */

		/* 1. disconnect from current db */
		sdb_engine_disconnect_from_db (dbe);

		/* 2. remove current db file */
		GFile *gfile = g_file_new_for_path (db_file);
		if (gfile != NULL) {
			g_file_delete (gfile, NULL, NULL);
			g_object_unref (gfile);
		}
		else 
		{
			g_warning ("Could not get the gfile");
		}		

		/* 3. reconnect */
		sdb_engine_connect_to_db (dbe, cnc_string, NULL);

		/* 4. create fresh new tables, indexes, triggers etc.  */			
		sdb_engine_create_db_tables (dbe, TABLES_SQL);
		return TRUE;
	}
	else
	{
		DEBUG_PRINT ("No need to upgrade.");
	}

	return FALSE;
}

/**
 * symbol_db_engine_open_db:
 * @dbe: self
 * @base_db_path: directory where .anjuta_sym_db.db will be stored. It can be
 *        different from project_directory
 *        E.g: a db on '/tmp/foo/' dir.
 * @prj_directory: project directory. It may be different from base_db_path.
 *        It's mainly used to map files inside the db. Say for example that you want to
 *        add to a project a file /home/user/project/foo_prj/src/file.c with a project
 *        directory of /home/user/project/foo_prj/. On db it'll be represented as
 *        src/file.c. In this way you can move around the project dir without dealing
 *        with relative paths.
 * @error: a place to store an error, or %NULL
 * 
 * Open, create or upgrade a database at given directory. 
 * Be sure to give a base_db_path with the ending '/' for directory.
 * 
 * Returns: An opening status from SymbolDBEngineOpenStatus enum.
 */
SymbolDBEngineOpenStatus
symbol_db_engine_open_db (SymbolDBEngine * dbe, const gchar * base_db_path,
						  const gchar * prj_directory, GError **error)
{
	SymbolDBEnginePriv *priv;
	gboolean needs_tables_creation = FALSE;
	gchar *cnc_string;
	gboolean connect_res;
	gboolean ret_status = DB_OPEN_STATUS_NORMAL;

	DEBUG_PRINT ("Opening project %s with base dir %s", 
				 prj_directory, base_db_path);
	
	g_return_val_if_fail (dbe != NULL, FALSE);
	g_return_val_if_fail (base_db_path != NULL, FALSE);

	priv = dbe->priv;

	priv->symbols_scanned_count = 0;
	
	/* check whether the db filename already exists. If it's not the case
	 * create the tables for the database. */
	gchar *db_file = g_strdup_printf ("%s/%s.db", base_db_path,
									   priv->anjuta_db_file);

	if (g_file_test (db_file, G_FILE_TEST_EXISTS) == FALSE)
	{
		needs_tables_creation = TRUE;
	}

	priv->db_directory = g_strdup (base_db_path);
	
	/* save the project_directory */
	priv->project_directory = g_strdup (prj_directory);

	 cnc_string = g_strdup_printf ("DB_DIR=%s;DB_NAME=%s", base_db_path,
								priv->anjuta_db_file);
	DEBUG_PRINT ("Connecting to "
				 "database with %s...", cnc_string);
	connect_res = sdb_engine_connect_to_db (dbe, cnc_string, error);
	

	if (connect_res == FALSE)
	{
		g_free (db_file);
		g_free (cnc_string);

		ret_status = DB_OPEN_STATUS_FATAL;
		return ret_status;
	}
	
	if (needs_tables_creation == TRUE)
	{
		DEBUG_PRINT ("Creating tables...");
		sdb_engine_create_db_tables (dbe, TABLES_SQL);
		ret_status = DB_OPEN_STATUS_CREATE;
	}
	else 
	{
		/* check the version of the db. If it's old we should upgrade it */
		if (sdb_engine_check_db_version_and_upgrade (dbe, db_file, cnc_string) == TRUE)
		{
			ret_status = DB_OPEN_STATUS_UPGRADE;
		}		
	}
	
	sdb_engine_set_defaults_db_parameters (dbe);

	g_free (cnc_string);
	g_free (db_file);

	/* we're now able to emit the db-connected signal: tables should be created
	 * and libgda should be connected to an usable db.
	 */
	g_signal_emit_by_name (dbe, "db-connected", NULL);
	
	return ret_status;
}

/**
 * symbol_db_engine_get_cnc_string:
 * @dbe: self
 * 
 * Getter for the connection string. 
 * 
 * Returns: The connection string. It must be freed by caller.
 */
gchar *
symbol_db_engine_get_cnc_string (SymbolDBEngine * dbe)
{
	SymbolDBEnginePriv *priv;

	g_return_val_if_fail (dbe != NULL, FALSE);	
	priv = dbe->priv;

	return g_strdup (priv->cnc_string);
}

/** 
 * symbol_db_engine_add_new_workspace:
 * @dbe: self
 * @workspace_name: name of workspace.
 * 
 * Add a new workspace to an opened database. 
 * ~~~ Thread note: this function locks the mutex ~~~
 * 
 * Returns: TRUE if operation is successful.
 */ 
gboolean
symbol_db_engine_add_new_workspace (SymbolDBEngine * dbe,
									const gchar * workspace_name)
{
	const GdaSet *plist;
	const GdaStatement *stmt;
	GdaHolder *param;
	SymbolDBEnginePriv *priv;
	GValue v = {0};

	g_return_val_if_fail (dbe != NULL, FALSE);	
	priv = dbe->priv;

	g_return_val_if_fail (priv->db_connection != NULL, FALSE);

	SDB_LOCK(priv);
	
	if ((stmt =
		 sdb_engine_get_statement_by_query_id (dbe, PREP_QUERY_WORKSPACE_NEW)) == NULL)
	{
		g_warning ("query is null");
		SDB_UNLOCK(priv);
		return FALSE;
	}

	plist = sdb_engine_get_query_parameters_list (dbe, PREP_QUERY_WORKSPACE_NEW);

	if ((param = gda_set_get_holder ((GdaSet*)plist, "wsname")) == NULL)
	{
		g_warning ("param is NULL from pquery!\n");
		SDB_UNLOCK(priv);
		return FALSE;
	}
	SDB_PARAM_SET_STRING(param, workspace_name);

	/* execute the query with parameters just set */
	if (gda_connection_statement_execute_non_select (priv->db_connection, 
														  (GdaStatement*)stmt, 
														  (GdaSet*)plist, NULL, NULL) == -1)
	{		
		SDB_UNLOCK(priv);
		return FALSE;
	}

	SDB_UNLOCK(priv);
	return TRUE;
}

/** 
 * symbol_db_engine_project_exists:
 * @dbe: self
 * @project_name: Project name.
 * @project_version: The version of the project.
 * 
 * Test project existence. 
 * ~~~ Thread note: this function locks the mutex ~~~
 * 
 * Returns: FALSE if project isn't found. TRUE otherwise.
 */
gboolean
symbol_db_engine_project_exists (SymbolDBEngine * dbe,
							   	const gchar * project_name,
    							const gchar * project_version)
{
	SymbolDBEnginePriv *priv;
	GValue v = {0};
	const GdaSet *plist;
	const GdaStatement *stmt;
	GdaHolder *param;
	GdaDataModel *data_model;

	priv = dbe->priv;

	SDB_LOCK(priv);
	
	g_return_val_if_fail (priv->db_connection != NULL, FALSE);
	
	/* test the existence of the project in db */
	/* get prepared query */
	if ((stmt = sdb_engine_get_statement_by_query_id (dbe, 
	    PREP_QUERY_GET_PROJECT_ID_BY_UNIQUE_NAME)) == NULL)
	{
		g_warning ("Query is null");
		SDB_UNLOCK(priv);
		return FALSE;
	}

	plist = sdb_engine_get_query_parameters_list (dbe, 
	    PREP_QUERY_GET_PROJECT_ID_BY_UNIQUE_NAME);
	
	if ((param = gda_set_get_holder ((GdaSet*)plist, "prjname")) == NULL)
	{
		g_warning ("sdb_engine_get_tuple_id_by_unique_name: param is NULL "
				   "from pquery!\n");
		SDB_UNLOCK(priv);
		return FALSE;
	}

	SDB_PARAM_SET_STRING (param, project_name);

	if ((param = gda_set_get_holder ((GdaSet*)plist, "prjversion")) == NULL)
	{
		g_warning ("sdb_engine_get_tuple_id_by_unique_name: param is NULL "
				   "from pquery!\n");
		SDB_UNLOCK(priv);
		return FALSE;
	}

	SDB_PARAM_SET_STRING (param, project_version);
	    
	/* execute the query with parameters just set */
	data_model = gda_connection_statement_execute_select (priv->db_connection, 
														  (GdaStatement*)stmt, 
														  (GdaSet*)plist, NULL);
		
	if (!GDA_IS_DATA_MODEL (data_model) ||
		gda_data_model_get_n_rows (GDA_DATA_MODEL (data_model)) <= 0)
	{
		if (data_model != NULL)
			g_object_unref (data_model);
		SDB_UNLOCK(priv);
		return FALSE;
	}

	/* we found it and we can return */
	g_object_unref (data_model);

	SDB_UNLOCK(priv);

	return TRUE;
}

/** 
 * symbol_db_engine_add_new_project:
 * @dbe: self
 * @workspace: Can be NULL. In that case a default workspace will be created, 
 *		and project will depend on that.
 * @project: Project name. Must NOT be NULL.
 * @version: Version of the project, or of the package that project represents. 
 * 		If not sure pass "1.0".
 * 
 * Adds a new project to db.
 * ~~~ Thread note: this function locks the mutex ~~~
 * 
 * Returns: TRUE if operation is successful.
 */
gboolean
symbol_db_engine_add_new_project (SymbolDBEngine * dbe, const gchar * workspace,
								  const gchar * project, const gchar* version)
{
	const GdaSet *plist;
	const GdaStatement *stmt;
	GdaHolder *param;
	const gchar *workspace_name;
	gint wks_id;
	SymbolDBEnginePriv *priv;
	GValue v = {0};

	g_return_val_if_fail (dbe != NULL, FALSE);	
	priv = dbe->priv;

	SDB_LOCK(priv);
	
	if (workspace == NULL)
	{
		workspace_name = "anjuta_workspace_default";	
		
		DEBUG_PRINT ("adding default workspace... '%s'", workspace_name);
		SDB_GVALUE_SET_STATIC_STRING(v, workspace_name);
		
		if ((wks_id = sdb_engine_get_tuple_id_by_unique_name (dbe,
				 PREP_QUERY_GET_WORKSPACE_ID_BY_UNIQUE_NAME,
				 "wsname",
				 &v)) <= 0)
		{ 
			
			/* symbol_db_engine_add_new_workspace 'll lock so unlock here before */			
			SDB_UNLOCK(priv);
			
			if (symbol_db_engine_add_new_workspace (dbe, workspace_name) == FALSE)
			{	
				DEBUG_PRINT ("%s", "Project cannot be added because a default workspace "
							 "cannot be created");				
				return FALSE;
			}
			/* relock */
			SDB_LOCK(priv);
		}
	}
	else
	{
		workspace_name = workspace;
	}

	g_value_unset (&v);
	
	/* insert new project */
	if ((stmt =
		 sdb_engine_get_statement_by_query_id (dbe, PREP_QUERY_PROJECT_NEW)) == NULL)
	{
		g_warning ("query is null");
		SDB_UNLOCK(priv);
		return FALSE;
	}

	plist = sdb_engine_get_query_parameters_list (dbe, PREP_QUERY_PROJECT_NEW);	
	
	/* lookup parameters */
	if ((param = gda_set_get_holder ((GdaSet*)plist, "prjname")) == NULL)
	{
		g_warning ("param prjname is NULL from pquery!");
		SDB_UNLOCK(priv);
		return FALSE;
	}	

	SDB_PARAM_SET_STRING(param, project);

	if ((param = gda_set_get_holder ((GdaSet*)plist, "prjversion")) == NULL)
	{
		g_warning ("param prjversion is NULL from pquery!");
		SDB_UNLOCK(priv);
		return FALSE;
	}	

	SDB_PARAM_SET_STRING(param, version);	
		
	if ((param = gda_set_get_holder ((GdaSet*)plist, "wsname")) == NULL)
	{
		g_warning ("param wsname is NULL from pquery!");
		SDB_UNLOCK(priv);
		return FALSE;
	}

	SDB_PARAM_SET_STRING(param, workspace_name);

	/* execute the query with parameters just set */
	if (gda_connection_statement_execute_non_select (priv->db_connection, 
														  (GdaStatement*)stmt, 
														  (GdaSet*)plist, NULL, NULL) == -1)
	{		
		SDB_UNLOCK(priv);
		return FALSE;
	}

	SDB_UNLOCK(priv);
	return TRUE;
}

/* ### Thread note: this function inherits the mutex lock ### */
/* Uses cache lookup to speed up symbols search. */
static gint
sdb_engine_add_new_language (SymbolDBEngine * dbe, const gchar *language)
{
	gint table_id;
	SymbolDBEnginePriv *priv;
	GValue v = {0};
	
	if (language == NULL)
		return -1;
	
	priv = dbe->priv;

	/* cache lookup */
	table_id = sdb_engine_cache_lookup (priv->language_cache, language);
	if (table_id != -1)
	{
		return table_id;
	}

	SDB_GVALUE_SET_STATIC_STRING (v, language);

	/* check for an already existing table with language "name". */
	if ((table_id = sdb_engine_get_tuple_id_by_unique_name (dbe,
						PREP_QUERY_GET_LANGUAGE_ID_BY_UNIQUE_NAME,
						"langname",
						&v)) < 0)
	{
		/* insert a new entry on db */
		const GdaSet *plist;
		const GdaStatement *stmt;
		GdaHolder *param;
		GdaSet *last_inserted = NULL;

		g_value_unset (&v);
		
		if ((stmt = sdb_engine_get_statement_by_query_id (dbe, PREP_QUERY_LANGUAGE_NEW))
			== NULL)
		{
			g_warning ("query is null");
			return FALSE;
		}

		plist = sdb_engine_get_query_parameters_list (dbe, PREP_QUERY_LANGUAGE_NEW);

		if ((param = gda_set_get_holder ((GdaSet*)plist, "langname")) == NULL)
		{
			g_warning ("param langname is NULL from pquery!");
			return FALSE;
		}

		SDB_PARAM_SET_STRING(param, language);
				
		/* execute the query with parameters just set */
		if (gda_connection_statement_execute_non_select (priv->db_connection, 
														 (GdaStatement*)stmt, 
														 (GdaSet*)plist, &last_inserted,
														 NULL) == -1)
		{		
			table_id = -1;
		}
		else {
			const GValue *value = gda_set_get_holder_value (last_inserted, "+0");
			table_id = g_value_get_int (value);
			sdb_engine_insert_cache (priv->language_cache, language, table_id);
		}		
		
		if (last_inserted)
			g_object_unref (last_inserted);
	}

	return table_id;
}

/**
 * ~~~ Thread note: this function locks the mutex ~~~
 *
 * Add a file to project. 
 * This function requires an opened db, i.e. calling before
 * symbol_db_engine_open_db ()
 * filepath: referes to a full file path.
 * project: 
 * WARNING: we suppose that project_directory is already set.
 * WARNING2: we suppose that the given local_filepath include the project_directory path.
 * + correct example: local_filepath: /home/user/projects/foo_project/src/main.c
 *                    project_directory: /home/user/projects/foo_project
 * - wrong one: local_filepath: /tmp/foo.c
 *                    project_directory: /home/user/projects/foo_project
 */
static gboolean
sdb_engine_add_new_db_file (SymbolDBEngine * dbe, const gchar * project_name,
    					const gchar *project_version,  const gchar * local_filepath, 
    					const gchar * language)
{
	const GdaSet *plist;
	const GdaStatement *stmt;
	GdaHolder *param;
	GError * error = NULL;
	SymbolDBEnginePriv *priv;
	gint language_id;
	GValue v = {0};
	
	priv = dbe->priv;

	/* check if the file is a correct one compared to the local_filepath */
	if (strstr (local_filepath, priv->project_directory) == NULL)
		return FALSE;

	SDB_LOCK(priv);

	/* we're gonna set the file relative to the project folder, not the full one.
	 * e.g.: we have a file on disk: "/tmp/foo/src/file.c" and a db_directory located on
	 * "/tmp/foo/". The entry on db will be "src/file.c" 
	 */
	const gchar *relative_path = symbol_db_util_get_file_db_path (dbe, local_filepath);
	if (relative_path == NULL)
	{
		DEBUG_PRINT ("%s", "relative_path == NULL");
		SDB_UNLOCK(priv);
		return FALSE;
	}	
	
	/* insert a new entry on db */	
	language_id = sdb_engine_add_new_language (dbe, language);
	if (language_id < 0)
	{
		DEBUG_PRINT ("Unknown language: %s", language);
		SDB_UNLOCK(priv);
		return FALSE;
	}

	if ((stmt = sdb_engine_get_statement_by_query_id (dbe, PREP_QUERY_FILE_NEW))
		== NULL)
	{
		g_warning ("query is null");
		SDB_UNLOCK(priv);
		return FALSE;
	}

	plist = sdb_engine_get_query_parameters_list (dbe, PREP_QUERY_FILE_NEW);
		
	/* filepath parameter */
	if ((param = gda_set_get_holder ((GdaSet*)plist, "filepath")) == NULL)
	{
		g_warning ("param langname is NULL from pquery!");
		SDB_UNLOCK(priv);
		return FALSE;
	}

	SDB_PARAM_SET_STRING(param, relative_path);
								
	/* project name parameter */
	if ((param = gda_set_get_holder ((GdaSet*)plist, "prjname")) == NULL)
	{
		g_warning ("param prjname is NULL from pquery!");
		SDB_UNLOCK(priv);
		return FALSE;
	}

	SDB_PARAM_SET_STRING(param, project_name);
	
	/* prjversion parameter */
	if ((param = gda_set_get_holder ((GdaSet*)plist, "prjversion")) == NULL)
	{
		g_warning ("param prjversion is NULL from pquery!");
		SDB_UNLOCK(priv);
		return FALSE;
	}

	SDB_PARAM_SET_STRING(param, project_version);
		
	/* language id parameter */
	if ((param = gda_set_get_holder ((GdaSet*)plist, "langid")) == NULL)
	{
		g_warning ("param langid is NULL from pquery!");
		SDB_UNLOCK(priv);
		return FALSE;
	}		

	SDB_PARAM_SET_INT(param, language_id);

	/* execute the query with parameters just set */
	if (gda_connection_statement_execute_non_select (priv->db_connection, 
													 (GdaStatement*)stmt, 
													 (GdaSet*)plist, NULL,
													 &error) == -1)
	{		
		if (error)
		{
			gchar * sql_str = gda_statement_to_sql_extended ((GdaStatement*)stmt, 
			    priv->db_connection, (GdaSet*)plist, 0, NULL, NULL);
			
			DEBUG_PRINT ("%s [%s]", error->message, sql_str);
			g_error_free (error);
			g_free (sql_str);
		}
		
		SDB_UNLOCK(priv);
		return FALSE;
	}	
	
	SDB_UNLOCK(priv);
	return TRUE;
} 

/* ~~~ Thread note: this function locks the mutex ~~~ */ 
static gint 
sdb_engine_get_unique_scan_id (SymbolDBEngine * dbe)
{
	SymbolDBEnginePriv *priv;
	gint ret_id;
	
	priv = dbe->priv;
	
	SDB_LOCK(priv);
	
	priv->scan_process_id_sequence++;	
	ret_id = priv->scan_process_id_sequence;
	
	SDB_UNLOCK(priv);
	return ret_id;
}

/**
 * symbol_db_engine_add_new_files_async:
 * @dbe: self
 * @lang_manager: IAnjutaLanguage language manager.
 * @project_name: 
 * @project_version: 
 * @sources_array: requires full path to files on disk. Anjuta-tags itself requires that.
 *        it must be something like "/home/path/to/my/foo/file.xyz". Also it requires
 *		  a language string to represent the file.
 *        An example of files_path array composition can be: 
 *        "/home/user/foo_project/foo1.c", "/home/user/foo_project/foo2.cpp", 
 * 		  "/home/user/foo_project/foo3.java".
 *        NOTE: all the files MUST exist. So check for their existence before call
 *        this function. The function'll write entries on the db.
 * 
 * See symbol_db_engine_add_new_files_full () for doc.
 * This function adds files to db in a quicker way than 
 * symbol_db_engine_add_new_files_full because you won't have to specify the
 * GPtrArray of languages, but it'll try to autodetect them.
 * When added, the files are forced to be scanned.
 *
 *
 * The function is suffixed with 'async'. This means that the scanning of the files is delayed
 * until the scanner is available. So you should use the gint id returned to identify
 * if a 'scan-end' signal is the one that you were expecting. 
 * Please note also that, if db is disconnected before the waiting queue is processed,
 * the scan of those files won't be performed.
 *
 * Returns: scan process id if insertion is successful, -1 on error.
 */
gint
symbol_db_engine_add_new_files_async (SymbolDBEngine *dbe, 
    							IAnjutaLanguage* lang_manager,
								const gchar * project_name,
    							const gchar * project_version,
							    const GPtrArray *sources_array)
{
	GPtrArray *lang_array;
	gint i;
	
	g_return_val_if_fail (dbe != NULL, FALSE);	
	g_return_val_if_fail (lang_manager != NULL, FALSE);	
	g_return_val_if_fail (sources_array != NULL, FALSE);

	lang_array = g_ptr_array_new_with_free_func (g_free);

	for (i = 0; i < sources_array->len; i++)
	{		
		IAnjutaLanguageId lang_id;
		GFile *gfile;
		GFileInfo *gfile_info;	
		const gchar *file_mime;
		const gchar *lang;
		const gchar *local_filename;
		
		local_filename = g_ptr_array_index (sources_array, i);			
		gfile = g_file_new_for_path (local_filename);		
		gfile_info = g_file_query_info (gfile, 
										"standard::content-type", 
										G_FILE_QUERY_INFO_NONE,
										NULL,
										NULL);
		if (gfile_info == NULL)
		{
			g_warning ("GFileInfo corresponding to %s was NULL", local_filename);
			g_object_unref (gfile);
			continue;
		}
		
		file_mime = g_file_info_get_attribute_string (gfile_info,
										  G_FILE_ATTRIBUTE_STANDARD_CONTENT_TYPE);										  		
					
		lang_id = ianjuta_language_get_from_mime_type (lang_manager, 
													 file_mime, NULL);
					
		if (!lang_id)
		{
			g_warning ("Language not found for %s was NULL", local_filename);
			g_object_unref (gfile);
			g_object_unref (gfile_info);			
			continue;
		}
				
		lang = ianjuta_language_get_name (lang_manager, lang_id, NULL);	
		g_ptr_array_add (lang_array, g_strdup (lang));
		g_object_unref (gfile);
		g_object_unref (gfile_info);
	}

	gint res = symbol_db_engine_add_new_files_full_async (dbe, project_name, project_version, 
	    sources_array, lang_array, TRUE);

	/* free resources */
	g_ptr_array_unref (lang_array);

	return res;
}

/** 
 * symbol_db_engine_add_new_files_full_async:
 * @dbe: self
 * @project_name: something like 'foo_project', or 'helloworld_project'. Can be NULL,
 *        for example when you're populating after abort.
 * @project_version: The version of the project.
 * @files_path: requires full path to files on disk. Anjuta-tags itself requires that.
 *        it must be something like "/home/path/to/my/foo/file.xyz". Also it requires
 *		  a language string to represent the file.
 *        An example of files_path array composition can be: 
 *        "/home/user/foo_project/foo1.c", "/home/user/foo_project/foo2.cpp", 
 * 		  "/home/user/foo_project/foo3.java".
 *        NOTE: all the files MUST exist. So check for their existence before call
 *        this function. The function'll write entries on the db.
 * @languages: is an array of 'languages'. It must have the same number of 
 *		  elments that files_path has. It should be populated like this: "C", "C++",
 *		  "Java" etc.
 * 		  This is done to be normalized with the language-manager plugin.
 * @force_scan: If FALSE a check on db will be done to see
 *		  whether the file is already present or not. In the latter care the scan will begin.
 * 
 * Add a group of files to a project. It will perform also 
 * a symbols scannig/populating of db if force_scan is TRUE.
 * This function requires an opened db, i.e. You must test db ststus with  
 * symbol_db_engine_open_db () before.
 * The function must be called from within the main thread.
 * 
 * Note: If some file fails to enter into the db the function will just skip them.
 * 
 *
 * The function is suffixed with 'async'. This means that the scanning of the files is delayed
 * until the scanner is available. So you should use the gint id returned to identify
 * if a 'scan-end' signal is the one that you were expecting. 
 * Please note also that, if db is disconnected before the waiting queue is processed,
 * the scan of those files won't be performed.
 *
 * Returns: scan process id if insertion is successful, -1 on error.
 */
gint
symbol_db_engine_add_new_files_full_async (SymbolDBEngine * dbe, 
								const gchar * project_name,
    							const gchar * project_version,
								const GPtrArray * files_path, 
								const GPtrArray * languages,
								gboolean force_scan)
{
	gint i;
	SymbolDBEnginePriv *priv;
	GPtrArray * filtered_files_path;
	gboolean ret_code;
	gint ret_id, scan_id;
	
	g_return_val_if_fail (dbe != NULL, FALSE);
	g_return_val_if_fail (files_path != NULL, FALSE);
	g_return_val_if_fail (languages != NULL, FALSE);
	priv = dbe->priv;
	
	g_return_val_if_fail (priv->db_connection != NULL, FALSE);
	g_return_val_if_fail (files_path->len > 0, FALSE);
	g_return_val_if_fail (languages->len > 0, FALSE);

	filtered_files_path = g_ptr_array_new ();
	
	for (i = 0; i < files_path->len; i++)
	{
		const gchar *node_file = (const gchar *) g_ptr_array_index (files_path, i);
		const gchar *node_lang = (const gchar *) g_ptr_array_index (languages, i);
		
		if (force_scan == FALSE)
		{
			/* test the existence of the file in db */
			if (symbol_db_engine_file_exists (dbe, node_file) == TRUE)
			{
				/* we don't want to touch the already present file... within 
				 * its symbols
				 */
				continue;
			}
		}
		
		if (project_name != NULL && 
			sdb_engine_add_new_db_file (dbe, project_name, project_version, node_file, 
									 node_lang) == FALSE)
		{
			DEBUG_PRINT ("Error processing file %s, db_directory %s, project_name %s, "
			    		"project_version %s, project_directory %s", node_file, 
					   	priv->db_directory, project_name, project_version, 
			    		priv->project_directory);
			continue;
		}
		
		/* note: we don't use g_strdup () here because we'll free the filtered_files_path
		 * before returning from this function.
		 */
		g_ptr_array_add (filtered_files_path, (gpointer)node_file);
	}

	/* perform the scan of files. It will spawn a fork() process with 
	 * AnjutaLauncher and ctags in server mode. After the ctags cmd has been 
	 * executed, the populating process'll take place.
	 */
	scan_id = sdb_engine_get_unique_scan_id (dbe);
	ret_code = sdb_engine_scan_files_async (dbe, filtered_files_path, NULL, FALSE, scan_id);
	
	if (ret_code == TRUE)
	{
		ret_id = scan_id;
	}
	else
		ret_id = -1;

	/* no need to free the items contained in the array */
	g_ptr_array_unref (filtered_files_path);
	return ret_id;
}


/**
 * We'll use the GNU regular expressions instead of the glib's GRegex ones because
 * the latters are a wrapper of pcre (www.pcre.org) that don't implement the 
 * \< (begin of word)) and \> (end of word) boundaries.
 * Since the regex used here is something complex to reproduce on GRegex 
 * I don't see any reason to reinvent the (already) working wheel.
 * I didn't find a valuable replacement for \< and \> neither on 
 * http://www.regular-expressions.info/wordboundaries.html nor elsewhere.
 * But if some regex geek thinks I'm wrong I'll be glad to see his solution.
 *
 * @return NULL on error.
 */
#define RX_STRING      "\\\".*\\\""
#define RX_BRACKETEXPR "\\{.*\\}"
#define RX_IDENT       "[a-zA-Z_][a-zA-Z0-9_]*"
#define RX_WS          "[ \t\n]*"
#define RX_PTR         "[\\*&]?\\*?"
#define RX_INITIALIZER "=(" RX_WS RX_IDENT RX_WS ")|=(" RX_WS RX_STRING RX_WS \
						")|=(" RX_WS RX_BRACKETEXPR RX_WS ")" RX_WS
#define RX_ARRAY 	   RX_WS "\\[" RX_WS "[0-9]*" RX_WS "\\]" RX_WS

static gchar* 
sdb_engine_extract_type_qualifier (const gchar *string, const gchar *expr)
{
	/* check with a regular expression for the type */
	regex_t re;
	regmatch_t pm[8]; // 7 sub expression -> 8 matches
	memset (&pm, -1, sizeof(pm));
	/*
	 * this regexp catches things like:
	 * a) std::vector<char*> exp1[124] [12], *exp2, expr;
	 * b) QClass* expr1, expr2, expr;
	 * c) int a,b; char r[12] = "test, argument", q[2] = { 't', 'e' }, expr;
	 *
	 * it CAN be fooled, if you really want it, but it should
	 * work for 99% of all situations.
	 *
	 * QString
	 * 		var;
	 * in 2 lines does not work, because //-comments would often bring wrong results
	 */

	gchar *res = NULL;
	static char pattern[512] =
		"(" RX_IDENT "\\>)" 	/* the 'std' in example a) */
		"(::" RX_IDENT ")*"	/* ::vector */
		"(" RX_WS "<[^>;]*>)?"	/* <char *> */
		/* other variables for the same ident (string i,j,k;) */
		"(" RX_WS RX_PTR RX_WS RX_IDENT RX_WS "(" RX_ARRAY ")*" "(" RX_INITIALIZER ")?," RX_WS ")*" 
		"[ \t\\*&]*";		/* check again for pointer/reference type */

	/* must add a 'termination' symbol to the regexp, otherwise
	 * 'exp' would match 'expr' 
	 */
	gchar regexp[512];
	g_snprintf (regexp, sizeof (regexp), "%s\\<%s\\>", pattern, expr);

	/* compile regular expression */
	int error = regcomp (&re, regexp, REG_EXTENDED) ;
	if (error)
		return NULL;

	/* this call to regexec finds the first match on the line */
	error = regexec (&re, string, 8, &pm[0], 0) ;

	/* while matches found */
	while (error == 0) 
	{   
		/* subString found between pm.rm_so and pm.rm_eo */
		/* only include the ::vector part in the indentifier, if the second 
		 * subpattern matches at all 
		 */
		int len = (pm[2].rm_so != -1 ? pm[2].rm_eo : pm[1].rm_eo) - pm[1].rm_so;
		if (res)
			free (res);
		res = (gchar*) g_malloc0 (len + 1);
		if (!res)
		{
			regfree (&re);
			return NULL;
		}
		strncpy (res, string + pm[1].rm_so, len); 
		res[len] = '\0';

		/* This call to regexec finds the next match */
		error = regexec (&re, string + pm[0].rm_eo, 8, &pm[0], 0) ;
		break;
	}
	regfree(&re);

	return res;
}

/* ### Thread note: this function inherits the mutex lock ### */
/* Uses cache lookup to speed up symbols search. */
static gint
sdb_engine_add_new_sym_kind (SymbolDBEngine * dbe, const tagEntry * tag_entry)
{
	const gchar *kind_name;
	gint table_id;
	SymbolDBEnginePriv *priv;
	GValue v = {0};
	
	priv = dbe->priv;
		
	/* we assume that tag_entry is != NULL */
	kind_name = tag_entry->kind;

	/* no kind associated with current tag */
	if (kind_name == NULL)
		return -1;

	/* cache lookup */
	table_id = sdb_engine_cache_lookup (priv->kind_cache, kind_name);
	if (table_id != -1)
	{
		return table_id;
	}

	SDB_GVALUE_SET_STATIC_STRING (v, kind_name);
	
	if ((table_id = sdb_engine_get_tuple_id_by_unique_name (dbe,
										PREP_QUERY_GET_SYM_KIND_BY_UNIQUE_NAME,
										"kindname",
										&v)) < 0)
	{
		const GdaSet *plist;
		const GdaStatement *stmt;
		GdaHolder *param;
		GdaSet *last_inserted = NULL;
		gint is_container = 0;
		SymType sym_type;
		GError * error = NULL;

		g_value_unset (&v);
		
		/* not found. Go on with inserting  */
		if ((stmt = sdb_engine_get_statement_by_query_id (dbe, PREP_QUERY_SYM_KIND_NEW))
			== NULL)
		{
			g_warning ("query is null");
			return -1;
		}

		plist = sdb_engine_get_query_parameters_list (dbe, PREP_QUERY_SYM_KIND_NEW);
		
		/* kindname parameter */
		if ((param = gda_set_get_holder ((GdaSet*)plist, "kindname")) == NULL)
		{
			g_warning ("param kindname is NULL from pquery!");
			return FALSE;
		}

		SDB_PARAM_SET_STRING (param, kind_name);

		/* container parameter */
		if ((param = gda_set_get_holder ((GdaSet*)plist, "container")) == NULL)
		{
			g_warning ("param container is NULL from pquery!");
			return FALSE;
		}

		sym_type = GPOINTER_TO_SIZE (g_hash_table_lookup (priv->sym_type_conversion_hash, 
		    									kind_name));
		
		if (sym_type & IANJUTA_SYMBOL_TYPE_SCOPE_CONTAINER)
			is_container = 1;

		SDB_PARAM_SET_INT (param, is_container);

		/* execute the query with parameters just set */
		if (gda_connection_statement_execute_non_select(priv->db_connection, 
														 (GdaStatement*)stmt, 
														 (GdaSet*)plist, &last_inserted,
														 NULL) == -1)
		{		
			table_id = -1;		
		}			
		else
		{
			const GValue *value = gda_set_get_holder_value (last_inserted, "+0");
			table_id = g_value_get_int (value);
			/* we should cache only tables which are != -1 */
			sdb_engine_insert_cache (priv->kind_cache, kind_name, table_id);
		}
		if (last_inserted)
			g_object_unref (last_inserted);		

		if (error)
		{
			g_warning ("SQL error: %s", error->message);
			g_error_free (error);
		}
	}

	return table_id;
}

/* ### Thread note: this function inherits the mutex lock ### */
/* Uses cache lookup to speed up symbols search. */
static gint
sdb_engine_add_new_sym_access (SymbolDBEngine * dbe, const tagEntry * tag_entry)
{
	const gchar *access;
	gint table_id;
	SymbolDBEnginePriv *priv;
	GValue v = {0};

	priv = dbe->priv;
		
	
	/* we assume that tag_entry is != NULL */	
	if ((access = tagsField (tag_entry, "access")) == NULL)
	{
		/* no access associated with current tag */
		return -1;
	}

	/* cache lookup */
	table_id = sdb_engine_cache_lookup (priv->access_cache, access);
	if (table_id != -1)
	{
		return table_id;
	}

	SDB_GVALUE_SET_STATIC_STRING (v, access);

	if ((table_id = sdb_engine_get_tuple_id_by_unique_name (dbe,
									PREP_QUERY_GET_SYM_ACCESS_BY_UNIQUE_NAME,
									"accesskind",
									&v)) < 0)
	{
		const GdaSet *plist;
		const GdaStatement *stmt;
		GdaHolder *param;
		GdaSet *last_inserted = NULL;

		g_value_unset (&v);
		
		/* not found. Go on with inserting  */
		if ((stmt =
			 sdb_engine_get_statement_by_query_id (dbe,
										 PREP_QUERY_SYM_ACCESS_NEW)) == NULL)
		{
 			g_warning ("query is null");
			return -1;
		}
		
		plist = sdb_engine_get_query_parameters_list (dbe, PREP_QUERY_SYM_ACCESS_NEW);
		
		/* accesskind parameter */
		if ((param = gda_set_get_holder ((GdaSet*)plist, "accesskind")) == NULL)
		{			
			g_warning ("param accesskind is NULL from pquery!");
			return -1;
		}

		SDB_PARAM_SET_STRING (param, access);
		
		/* execute the query with parameters just set */
		if (gda_connection_statement_execute_non_select (priv->db_connection, 
														 (GdaStatement*)stmt, 
														 (GdaSet*)plist, &last_inserted,
														 NULL) == -1)
		{		
			table_id = -1;		
		}			
		else
		{
			const GValue *value = gda_set_get_holder_value (last_inserted, "+0");
			table_id = g_value_get_int (value);
			/* we should cache only tables which are != -1 */
			sdb_engine_insert_cache (priv->access_cache, access, table_id);
		}		
		
		if (last_inserted)
			g_object_unref (last_inserted);
	}

	return table_id;
}

/* ### Thread note: this function inherits the mutex lock ### */
/* Uses cache lookup to speed up symbols search. */
static gint
sdb_engine_add_new_sym_implementation (SymbolDBEngine * dbe,
									   const tagEntry * tag_entry)
{
	const gchar *implementation;
	gint table_id;
	SymbolDBEnginePriv *priv;
	GValue v = {0};

	priv = dbe->priv;
			
	/* we assume that tag_entry is != NULL */	
	if ((implementation = tagsField (tag_entry, "implementation")) == NULL)
	{
		/* no implementation associated with current tag */
		return -1;
	}

	/* cache lookup */
	table_id = sdb_engine_cache_lookup (priv->implementation_cache, implementation);
	if (table_id != -1)
	{
		return table_id;
	}

	SDB_GVALUE_SET_STATIC_STRING(v, implementation);

	if ((table_id = sdb_engine_get_tuple_id_by_unique_name (dbe,
							PREP_QUERY_GET_SYM_IMPLEMENTATION_BY_UNIQUE_NAME,
							"implekind",
							&v)) < 0)
	{
		const GdaSet *plist;
		const GdaStatement *stmt;
		GdaHolder *param;
		GdaSet *last_inserted = NULL;

		g_value_unset (&v);
		
		/* not found. Go on with inserting  */
		if ((stmt = sdb_engine_get_statement_by_query_id (dbe,
										 PREP_QUERY_SYM_IMPLEMENTATION_NEW)) ==
			NULL)
		{
			g_warning ("query is null");
			return -1;
		}

		plist = sdb_engine_get_query_parameters_list (dbe, 
		    PREP_QUERY_SYM_IMPLEMENTATION_NEW);
		
		/* implekind parameter */
		if ((param = gda_set_get_holder ((GdaSet*)plist, "implekind")) == NULL)
		{			
			g_warning ("param accesskind is NULL from pquery!");
			return -1;
		}

		SDB_PARAM_SET_STRING(param, implementation);
		
		/* execute the query with parameters just set */
		if (gda_connection_statement_execute_non_select (priv->db_connection, 
														 (GdaStatement*)stmt, 
														 (GdaSet*)plist, &last_inserted,
														 NULL) == -1)
		{		
			table_id = -1;
		}			
		else
		{
			const GValue *value = gda_set_get_holder_value (last_inserted, "+0");
			table_id = g_value_get_int (value);			
			/* we should cache only tables which are != -1 */
			sdb_engine_insert_cache (priv->implementation_cache, implementation, 
									 table_id);	
		}
		if (last_inserted)
			g_object_unref (last_inserted);	
	}
	
	return table_id;
}

/* ### Thread note: this function inherits the mutex lock ### */
static void
sdb_engine_add_new_heritage (SymbolDBEngine * dbe, gint base_symbol_id,
							 gint derived_symbol_id)
{
	const GdaSet *plist;
	const GdaStatement *stmt;
	GdaHolder *param;
	SymbolDBEnginePriv *priv;
	GValue v = {0};	
	
	g_return_if_fail (base_symbol_id > 0);
	g_return_if_fail (derived_symbol_id > 0);

	priv = dbe->priv;
	
	if ((stmt = sdb_engine_get_statement_by_query_id (dbe, PREP_QUERY_HERITAGE_NEW))
		== NULL)
	{
		g_warning ("query is null");
		return;
	}

	plist = sdb_engine_get_query_parameters_list (dbe, PREP_QUERY_HERITAGE_NEW);
		
	/* symbase parameter */
	if ((param = gda_set_get_holder ((GdaSet*)plist, "symbase")) == NULL)
	{			
		g_warning ("param accesskind is NULL from pquery!");
		return;
	}

	SDB_PARAM_SET_INT(param, base_symbol_id);

	/* symderived id parameter */
	if ((param = gda_set_get_holder ((GdaSet*)plist, "symderived")) == NULL)
	{
		g_warning ("param symderived is NULL from pquery!");
		return;
	}

	SDB_PARAM_SET_INT(param, derived_symbol_id);	

	/* execute the query with parameters just set */
	if (gda_connection_statement_execute_non_select (priv->db_connection, 
													 (GdaStatement*)stmt, 
													 (GdaSet*)plist, NULL,
													 NULL) == -1)
	{		
		g_warning ("Error adding heritage");
	}	
}
             

/* ### Thread note: this function inherits the mutex lock ### */
static GNUC_INLINE gint
sdb_engine_add_new_scope_definition (SymbolDBEngine * dbe, const tagEntry * tag_entry)
{
	const gchar *scope;
	gint table_id;
	const GdaSet *plist;
	const GdaStatement *stmt;
	GdaHolder *param;
	GdaSet *last_inserted = NULL;
	SymbolDBEnginePriv *priv;
	GValue v = {0};

	g_return_val_if_fail (tag_entry->kind != NULL, -1);

	priv = dbe->priv;
	
	
	/* This symbol will define a scope which name is tag_entry->name
	 * For example if we get a tag MyFoo with kind "namespace", it will define 
	 * the "MyFoo" scope, which type is "namespace MyFoo"
	 */
	scope = tag_entry->name;

	/* filter out 'variable' and 'member' kinds. They define no scope. */
	if (g_strcmp0 (tag_entry->kind, "variable") == 0 ||
		g_strcmp0 (tag_entry->kind, "member") == 0)
	{
		return -1;
	}
	
	if ((stmt = sdb_engine_get_statement_by_query_id (dbe, PREP_QUERY_SCOPE_NEW))
		== NULL)
	{
		g_warning ("query is null");
		return -1;
	}

	plist = sdb_engine_get_query_parameters_list (dbe, PREP_QUERY_SCOPE_NEW);
		
	/* scope parameter */
	if ((param = gda_set_get_holder ((GdaSet*)plist, "scope")) == NULL)
	{			
		g_warning ("param scope is NULL from pquery!");
		return -1;
	}

	SDB_PARAM_SET_STRING (param, scope);

	/* execute the query with parameters just set */
	if (gda_connection_statement_execute_non_select (priv->db_connection, 
													 (GdaStatement*)stmt, 
													 (GdaSet*)plist, &last_inserted,
													 NULL) == -1)
	{
	
		GValue v = {0, };
		SDB_GVALUE_SET_STATIC_STRING(v, scope);
		
		/* try to get an already existing scope */
		table_id = sdb_engine_get_tuple_id_by_unique_name (dbe, PREP_QUERY_GET_SCOPE_ID,
													"scope", &v);
	}
	else  
	{
		const GValue *value = gda_set_get_holder_value (last_inserted, "+0");
		table_id = g_value_get_int (value);
	}	

	if (last_inserted)
		g_object_unref (last_inserted);	

	return table_id;
}

/**
 * ### Thread note: this function inherits the mutex lock ### 
 *
 * Saves the tagEntry info for a second pass parsing.
 * Usually we don't know all the symbol at the first scan of the tags. We need
 * a second one. 
 *
 */
static GNUC_INLINE void
sdb_engine_add_new_tmp_heritage_scope (SymbolDBEngine * dbe,
									   const tagEntry * tag_entry,
									   gint symbol_referer_id)
{
	SymbolDBEnginePriv *priv;
	const gchar *field_inherits, *field_struct, *field_typeref,
		*field_enum, *field_union, *field_class, *field_namespace;
	TableMapTmpHeritage * node;

	priv = dbe->priv;

	node = g_slice_new0 (TableMapTmpHeritage);	
	node->symbol_referer_id = symbol_referer_id;

	if ((field_inherits = tagsField (tag_entry, "inherits")) != NULL)
	{
		node->field_inherits = g_strdup (field_inherits);
	}

	if ((field_struct = tagsField (tag_entry, "struct")) != NULL)
	{
		node->field_struct = g_strdup (field_struct);
	}

	if ((field_typeref = tagsField (tag_entry, "typeref")) != NULL)
	{
		node->field_typeref = g_strdup (field_typeref);
	}

	if ((field_enum = tagsField (tag_entry, "enum")) != NULL)
	{
		node->field_enum = g_strdup (field_enum);
	}

	if ((field_union = tagsField (tag_entry, "union")) != NULL)
	{
		node->field_union = g_strdup (field_union);
	}
 
	if ((field_class = tagsField (tag_entry, "class")) != NULL)
	{
		node->field_class = g_strdup (field_class);
	}

	if ((field_namespace = tagsField (tag_entry, "namespace")) != NULL)
	{
		node->field_namespace = g_strdup (field_namespace);
	}

	g_queue_push_head (priv->tmp_heritage_tablemap, node);
}

/** 
 * ### Thread note: this function inherits the mutex lock ### 
 *
 * Return the symbol_id of the changed symbol 
 */
static GNUC_INLINE void
sdb_engine_second_pass_update_scope_1 (SymbolDBEngine * dbe,
									   TableMapTmpHeritage * node,
									   gchar * token_name,
									   const gchar * token_value)
{
	gint symbol_referer_id;
	const gchar *tmp_str;
	gchar **tmp_str_splitted;
	gint tmp_str_splitted_length;
	gchar *object_name = NULL;
	gboolean free_token_name = FALSE;
	const GdaSet *plist;
	const GdaStatement *stmt;
	GdaHolder *param;
	SymbolDBEnginePriv *priv;
	GValue v = {0};

	g_return_if_fail (token_value != NULL);
		
	priv = dbe->priv;
	tmp_str = token_value;

	/* we don't need empty strings */
	if (strlen (tmp_str) <= 0)
	{
		return;
	}

	/* we could have something like "First::Second::Third::Fourth" as tmp_str, so 
	 * take only the lastscope, in this case 'Fourth'.
	 */
	tmp_str_splitted = g_strsplit (tmp_str, ":", 0);
	tmp_str_splitted_length = g_strv_length (tmp_str_splitted);

	if (tmp_str_splitted_length > 0)
	{
		/* handle special typedef case. Usually we have something like struct:my_foo.
		 * splitting we have [0]-> struct [1]-> my_foo
		 */
		if (g_strcmp0 (token_name, "typedef") == 0)
		{
			free_token_name = TRUE;
			token_name = g_strdup (tmp_str_splitted[0]);
		}

		object_name = g_strdup (tmp_str_splitted[tmp_str_splitted_length - 1]);
	}
	else
	{
		g_strfreev (tmp_str_splitted);
		return;
	}

	g_strfreev (tmp_str_splitted);

	/* if we reach this point we should have a good scope_id.
	 * Go on with symbol updating.
	 */
	symbol_referer_id = node->symbol_referer_id;
		
	if ((stmt = sdb_engine_get_statement_by_query_id (dbe,
											 PREP_QUERY_UPDATE_SYMBOL_SCOPE_ID))
		== NULL)
	{
		g_warning ("query is null");
		return;
	}

	plist = sdb_engine_get_query_parameters_list (dbe, PREP_QUERY_UPDATE_SYMBOL_SCOPE_ID);

	/* tokenname parameter */
	if ((param = gda_set_get_holder ((GdaSet*)plist, "tokenname")) == NULL)
	{
		g_warning ("param tokenname is NULL from pquery!");
		return;
	}

	SDB_PARAM_SET_STRING(param, token_name);

	/* objectname parameter */
	if ((param = gda_set_get_holder ((GdaSet*)plist, "objectname")) == NULL)
	{
		g_warning ("param objectname is NULL from pquery!");
		return;
	}

	SDB_PARAM_SET_STRING(param, object_name);

	/* symbolid parameter */
	if ((param = gda_set_get_holder ((GdaSet*)plist, "symbolid")) == NULL)
	{
		g_warning ("param symbolid is NULL from pquery!");
		return;
	}

	SDB_PARAM_SET_INT(param, symbol_referer_id);

	/* execute the query with parameters just set */
	gda_connection_statement_execute_non_select (priv->db_connection, 
													 (GdaStatement*)stmt, 
													 (GdaSet*)plist, NULL,
													 NULL);

	if (free_token_name)
		g_free (token_name);
	g_free (object_name);
	
	return;
}

/**
 * ### Thread note: this function inherits the mutex lock ### 
 *
 * @param data Must be filled with some values. It must have num_rows > 0
 * @note *CALL THIS BEFORE second_pass_update_heritage ()*
 * @note *DO NOT FREE data* inside this function.
 */
static void
sdb_engine_second_pass_update_scope (SymbolDBEngine * dbe)
{
	SymbolDBEnginePriv *priv;
	/*
	 * Fill up the scope. 
	 * The case: "my_foo_func_1" is the name of the current tag parsed. 
	 * Suppose we have a namespace MyFooNamespace, under which is declared
	 * a class MyFooClass. Under that class there are some funcs like 
	 * my_foo_func_1 () etc. ctags will present us this info about 
	 * my_foo_func_1 ():
	 * "class : MyFooNamespace::MyFooClass"
	 * but hey! We don't need to know the namespace here, we just want to 
	 * know that my_foo_func_1 is in the scope of MyFooClass. That one will 
	 * then be mapped inside MyFooNamespace, but that's another thing.
	 * Go on with the parsing then.
	 */
	gint i;
	gsize queue_length;
	
	priv = dbe->priv;	
	
	DEBUG_PRINT ("Processing %d rows", g_queue_get_length (priv->tmp_heritage_tablemap));

	/* get a fixed length. There may be some tail_pushes during this loop */
	queue_length = g_queue_get_length (priv->tmp_heritage_tablemap);
	
	for (i = 0; i < queue_length; i++)
	{
		TableMapTmpHeritage *node;
		node = g_queue_pop_head (priv->tmp_heritage_tablemap);
		
		if (node->field_class != NULL)
		{
			sdb_engine_second_pass_update_scope_1 (dbe, node, "class", node->field_class);
		}

		if (node->field_struct != NULL)
		{		
			sdb_engine_second_pass_update_scope_1 (dbe, node, "struct", node->field_struct);
		}

		if (node->field_typeref != NULL)
		{
			/* this is a "typedef", not a "typeref". */
			sdb_engine_second_pass_update_scope_1 (dbe, node, "typedef", node->field_typeref);
		}

		if (node->field_enum != NULL)
		{
			sdb_engine_second_pass_update_scope_1 (dbe, node, "enum", node->field_enum);
		}

		if (node->field_union != NULL)
		{
			sdb_engine_second_pass_update_scope_1 (dbe, node, "union", node->field_union);
		}

		if (node->field_namespace != NULL)
		{
			sdb_engine_second_pass_update_scope_1 (dbe, node, "namespace", node->field_namespace);
		}

		/* last check: if inherits is not null keep the node for a later task */
		if (node->field_inherits != NULL)
		{
			g_queue_push_tail (priv->tmp_heritage_tablemap, node);
		}
		else 
		{
			sdb_engine_tablemap_tmp_heritage_destroy (node);
		}
	}

}

/**
 * ### Thread note: this function inherits the mutex lock ### 
 *
 * @param data Must be filled with some values. It must have num_rows > 0
 * @note *CALL THIS AFTER second_pass_update_scope ()*
 */
static void
sdb_engine_second_pass_update_heritage (SymbolDBEngine * dbe)
{
#if 0
	gint i;
	SymbolDBEnginePriv *priv;
	
	g_return_if_fail (dbe != NULL);
	
	priv = dbe->priv;
	
	DEBUG_PRINT ("Updating heritage... (%d) elements", 
	    g_queue_get_length (priv->tmp_heritage_tablemap));
	
	for (i = 0; i < g_queue_get_length (priv->tmp_heritage_tablemap); i++)
	{
		const gchar *inherits;
		gchar *item;
		gchar **inherits_list;
		gint j;
		TableMapTmpHeritage *node;

		node = g_queue_pop_head (priv->tmp_heritage_tablemap);		
		inherits = node->field_inherits;

		if (inherits == NULL)
		{
			g_warning ("Inherits was NULL on sym_referer id %d", 
			    node->symbol_referer_id);
			sdb_engine_tablemap_tmp_heritage_destroy (node);
			continue;
		}

		/* there can be multiple inheritance. Check that. */
		inherits_list = g_strsplit (inherits, ",", 0);

		if (inherits_list != NULL)
			DEBUG_PRINT ("inherits %s", inherits);

		/* retrieve as much info as we can from the items */
		for (j = 0; j < g_strv_length (inherits_list); j++)
		{
			gchar **namespaces;
			gchar *klass_name;
			gchar *namespace_name;
			gint namespaces_length;
			gint base_klass_id;
			gint derived_klass_id;

			item = inherits_list[j];
			DEBUG_PRINT ("heritage %s", item);

			/* A item may have this string form:
			 * MyFooNamespace1::MyFooNamespace2::MyFooClass
			 * We should find the field 'MyFooNamespace2' because it's the one 
			 * that is reachable by the scope_id value of the symbol.
			 */

			namespaces = g_strsplit (item, "::", 0);
			namespaces_length = g_strv_length (namespaces);

			if (namespaces_length > 1)
			{
				/* this is the case in which we have the case with 
				 * namespace + class
				 */
				namespace_name = g_strdup (namespaces[namespaces_length - 2]);
				klass_name = g_strdup (namespaces[namespaces_length - 1]);
			}
			else
			{
				/* have a last check before setting namespace_name to null.
				 * check whether the field_namespace is void or not.
				 */
				const gchar *tmp_namespace;
				gchar **tmp_namespace_array = NULL;
				gint tmp_namespace_length;

				tmp_namespace = node->field_namespace;
				if (tmp_namespace != NULL)
				{
					tmp_namespace_array = g_strsplit (tmp_namespace, "::", 0);
					tmp_namespace_length = g_strv_length (tmp_namespace_array);

					if (tmp_namespace_length > 0)
					{
						namespace_name =
							g_strdup (tmp_namespace_array
									  [tmp_namespace_length - 1]);
					}
					else
					{
						namespace_name = NULL;
					}
				}
				else
				{
					namespace_name = NULL;
				}

				klass_name = g_strdup (namespaces[namespaces_length - 1]);

				g_strfreev (tmp_namespace_array);
			}

			g_strfreev (namespaces);

			/* get the derived_klass_id. It should be the 
			 * symbol_referer_id field into __tmp_heritage_scope table
			 */
			if (node->symbol_referer_id > 0)
			{
				derived_klass_id = node->symbol_referer_id;
			}
			else
			{
				derived_klass_id = 0;
			}

			/* ok, search for the symbol_id of the base class */
			if (namespace_name == NULL)
			{
				GValue *value1;

				MP_LEND_OBJ_STR (priv, value1);
				g_value_set_static_string (value1, klass_name);
				
				if ((base_klass_id =
					 sdb_engine_get_tuple_id_by_unique_name (dbe,
										 PREP_QUERY_GET_SYMBOL_ID_BY_CLASS_NAME,
										 "klassname",
										 value1)) < 0)
				{
					continue;
				}
			}
			else
			{
				GValue *value1;
				GValue *value2;

				MP_LEND_OBJ_STR (priv, value1);
				g_value_set_static_string (value1, klass_name);

				MP_LEND_OBJ_STR (priv, value2);
				g_value_set_static_string (value2, namespace_name);

				if ((base_klass_id =
					 sdb_engine_get_tuple_id_by_unique_name2 (dbe,
						  PREP_QUERY_GET_SYMBOL_ID_BY_CLASS_NAME_AND_NAMESPACE,
						  "klassname",
						  value1,
						  "namespacename",
						  value2)) < 0)
				{
					continue;
				}
			}

			g_free (namespace_name);
			g_free (klass_name);
			
			DEBUG_PRINT ("gonna sdb_engine_add_new_heritage with "
						 "base_klass_id %d, derived_klass_id %d", base_klass_id, 
						 derived_klass_id);
			sdb_engine_add_new_heritage (dbe, base_klass_id, derived_klass_id);
		}

		g_strfreev (inherits_list);
	}	
#endif	
}

/**
 * ### Thread note: this function inherits the mutex lock ### 
 *
 * Process the temporary table to update the symbols on scope and inheritance 
 * fields.
 * *CALL THIS FUNCTION ONLY AFTER HAVING PARSED ALL THE TAGS ONCE*
 *
 */
static void
sdb_engine_second_pass_do (SymbolDBEngine * dbe)
{
	SymbolDBEnginePriv *priv;

	priv = dbe->priv;

	/* prepare for scope second scan */
	if (g_queue_get_length (priv->tmp_heritage_tablemap) > 0)
	{
		sdb_engine_second_pass_update_scope (dbe);
		sdb_engine_second_pass_update_heritage (dbe);
	}
}

GNUC_INLINE static void
sdb_engine_add_new_symbol_case_1 (SymbolDBEngine *dbe,
    							  gint symbol_id,
    							  GdaSet **plist_ptr,
								  GdaStatement **stmt_ptr)
{
	GdaHolder *param;
	GValue v = {0};

	const GdaSet * plist = *plist_ptr;
	const GdaStatement * stmt = *stmt_ptr;
	
	/* case 1 */
	
	/* create specific query for a fresh new symbol */
	if ((stmt = sdb_engine_get_statement_by_query_id (dbe,
											 PREP_QUERY_UPDATE_SYMBOL_ALL))
		== NULL)
	{
		g_warning ("query is null");
		return;
	}

	plist = sdb_engine_get_query_parameters_list (dbe, PREP_QUERY_UPDATE_SYMBOL_ALL);
		
	/* symbolid parameter */
	if ((param = gda_set_get_holder ((GdaSet*)plist, "symbolid")) == NULL)
	{
		g_warning ("param isfilescope is NULL from pquery!");
		return;
	}

	SDB_PARAM_SET_INT(param, symbol_id);
	
	*plist_ptr = (GdaSet*)plist;
	*stmt_ptr = (GdaStatement*)stmt;	
}

GNUC_INLINE static void
sdb_engine_add_new_symbol_case_2_3 (SymbolDBEngine *dbe,
    							  gint symbol_id,
    							  GdaSet **plist_ptr,
								  GdaStatement **stmt_ptr,
    							  gint file_defined_id,
    							  const gchar *name,
    							  const gchar *type_type,
    							  const gchar *type_name)
{
	GdaHolder *param;
	GValue v = {0};

	const GdaSet * plist = *plist_ptr;
	const GdaStatement * stmt = *stmt_ptr;

	/* create specific query for a fresh new symbol */
	if ((stmt = sdb_engine_get_statement_by_query_id (dbe, PREP_QUERY_SYMBOL_NEW))
		== NULL)
	{
		g_warning ("query is null");
		return;
	}

	plist = sdb_engine_get_query_parameters_list (dbe, PREP_QUERY_SYMBOL_NEW);
	
	/* filedefid parameter */
	if ((param = gda_set_get_holder ((GdaSet*)plist, "filedefid")) == NULL)
	{
		g_warning ("param filedefid is NULL from pquery!");
		return;
	}

	SDB_PARAM_SET_INT(param, file_defined_id);

	/* name parameter */
	if ((param = gda_set_get_holder ((GdaSet*)plist, "name")) == NULL)
	{
		g_warning ("param name is NULL from pquery!");			
		return;
	}

	SDB_PARAM_SET_STRING(param, name);

	/* typetype parameter */
	if ((param = gda_set_get_holder ((GdaSet*)plist, "typetype")) == NULL)
	{
		g_warning ("param typetype is NULL from pquery!");
		return;			
	}

	SDB_PARAM_SET_STRING(param, type_type);

	/* typenameparameter */
	if ((param = gda_set_get_holder ((GdaSet*)plist, "typename")) == NULL)
	{
		g_warning ("param typename is NULL from pquery!");
		return;			
	}

	SDB_PARAM_SET_STRING(param, type_name);

	*plist_ptr = (GdaSet*)plist;
	*stmt_ptr = (GdaStatement*)stmt;
}

GNUC_INLINE static void
sdb_engine_add_new_symbol_common_params (SymbolDBEngine *dbe, 
										 const GdaSet *plist,
								  		 const GdaStatement *stmt,
    									 gint file_position,
    									 gint is_file_scope,
										 const gchar *signature,	
    									 const gchar *returntype,
    								     gint scope_definition_id,    							  		 
    									 gint scope_id,
    									 gint kind_id,
    									 gint access_kind_id,
    									 gint implementation_kind_id,
    									 gboolean update_flag)
{
	GdaHolder *param;
	GValue v = {0};

	/* fileposition parameter */
	if ((param = gda_set_get_holder ((GdaSet*)plist, "fileposition")) == NULL)
	{
		g_warning ("param fileposition is NULL from pquery!");
		return;
	}

	SDB_PARAM_SET_INT (param, file_position);
	
	/* isfilescope parameter */
	if ((param = gda_set_get_holder ((GdaSet*)plist, "isfilescope")) == NULL)	
	{
		g_warning ("param isfilescope is NULL from pquery!");
		return;
	}

	SDB_PARAM_SET_INT (param, is_file_scope);
	
	/* signature parameter */
	if ((param = gda_set_get_holder ((GdaSet*)plist, "signature")) == NULL)	
	{
		g_warning ("param signature is NULL from pquery!");
		return;
	}

	SDB_PARAM_SET_STRING(param, signature);

	/* returntype parameter */
	if ((param = gda_set_get_holder ((GdaSet*)plist, "returntype")) == NULL)	
	{
		g_warning ("param returntype is NULL from pquery!");
		return;
	}

	SDB_PARAM_SET_STRING(param, returntype);
	
	/* scopedefinitionid parameter */
	if ((param = gda_set_get_holder ((GdaSet*)plist, "scopedefinitionid")) == NULL)	
	{
		g_warning ("param scopedefinitionid is NULL from pquery!");
		return;
	}

	SDB_PARAM_SET_INT(param, scope_definition_id);

	/* scopeid parameter */
	if ((param = gda_set_get_holder ((GdaSet*)plist, "scopeid")) == NULL)	
	{
		g_warning ("param scopeid is NULL from pquery!");
		return;
	}

	SDB_PARAM_SET_INT(param, scope_id);
	
	/* kindid parameter */
	if ((param = gda_set_get_holder ((GdaSet*)plist, "kindid")) == NULL)	
	{
		g_warning ("param kindid is NULL from pquery!");
		return;
	}

	SDB_PARAM_SET_INT(param, kind_id);
	
	/* accesskindid parameter */
	if ((param = gda_set_get_holder ((GdaSet*)plist, "accesskindid")) == NULL)	
	{
		g_warning ("param accesskindid is NULL from pquery!");
		return;
	}

	SDB_PARAM_SET_INT(param, access_kind_id);

	/* implementationkindid parameter */
	if ((param = gda_set_get_holder ((GdaSet*)plist, "implementationkindid")) == NULL)	
	{
		g_warning ("param implementationkindid is NULL from pquery!");
		return;
	}

	SDB_PARAM_SET_INT(param, implementation_kind_id);
	
	/* updateflag parameter */
	if ((param = gda_set_get_holder ((GdaSet*)plist, "updateflag")) == NULL)
	{
		g_warning ("param updateflag is NULL from pquery!");
		return;
	}

	SDB_PARAM_SET_INT(param, update_flag);
}


/**
 * ### Thread note: this function inherits the mutex lock ### 
 *
 * base_prj_path can be NULL. In that case path info tag_entry will be taken
 * as an absolute path.
 * fake_file can be used when a buffer updating is being executed. In that 
 * particular case both base_prj_path and tag_entry->file will be ignored. 
 * fake_file is real_path of file on disk
 */
static gint
sdb_engine_add_new_symbol (SymbolDBEngine * dbe, const tagEntry * tag_entry,
						   gint file_defined_id,
						   gboolean sym_update)
{
	SymbolDBEnginePriv *priv;
	GdaSet *plist;
	GdaStatement *stmt;
	GdaSet *last_inserted = NULL;
	gint table_id, symbol_id;
	const gchar* name;
	gint file_position = 0;
	gint is_file_scope = 0;
	const gchar *signature;
	const gchar *returntype;
	gint scope_definition_id = 0;
	gint scope_id = 0;
	gint kind_id = 0;
	gint access_kind_id = 0;
	gint implementation_kind_id = 0;
	GValue v1 = {0}, v2 = {0}, v3 = {0}, v4 = {0}, v5 = {0};
	gboolean sym_was_updated = FALSE;
	gboolean update_flag;
	gchar *type_regex;;
	const gchar *type_type;
	const gchar *type_name;
	gint nrows;
	GError * error = NULL;
	
	g_return_val_if_fail (dbe != NULL, -1);
	priv = dbe->priv;
	
	/* keep it at 0 if sym_update == false */
	update_flag = sym_update;

	g_return_val_if_fail (tag_entry != NULL, -1);

	/* parse the entry name */
	name = tag_entry->name;
	file_position = tag_entry->address.lineNumber;
	is_file_scope = tag_entry->fileScope;

	/* 
	 * signature 
	 */
	signature = tagsField (tag_entry, "signature");	

	/*
	 * return type
	 */
	returntype = tagsField (tag_entry, "returntype");	

	/* 
	 * sym_type
	 */
	/* we assume that tag_entry is != NULL */
	type_type = tag_entry->kind;
	type_regex = NULL;
	
	if (g_strcmp0 (type_type, "member") == 0 || 
	    g_strcmp0 (type_type, "variable") == 0 || 
	    g_strcmp0 (type_type, "field") == 0)
	{
		type_regex = sdb_engine_extract_type_qualifier (tag_entry->address.pattern, 
		                                                tag_entry->name);
		/*DEBUG_PRINT ("type_regex for %s [kind %s] is %s", tag_entry->name, 
		             tag_entry->kind, type_regex);*/
		type_name = type_regex;

		/* if the extractor failed we should fallback to the default one */
		if (type_name == NULL)
			type_name = tag_entry->name;
	}	else 
	{
		type_name = tag_entry->name;
	}
	

	/*
	 * scope definition
	 */
	 
	/* scope_definition_id tells what scope this symbol defines */
	scope_definition_id = sdb_engine_add_new_scope_definition (dbe, tag_entry);

	/* the container scopes can be: union, struct, typeref, class, namespace etc.
	 * this field will be parsed in the second pass.
	 */
	scope_id = 0;

	kind_id = sdb_engine_add_new_sym_kind (dbe, tag_entry);
	
	access_kind_id = sdb_engine_add_new_sym_access (dbe, tag_entry);
	
	implementation_kind_id = sdb_engine_add_new_sym_implementation (dbe, tag_entry);
	
	/* ok: was the symbol updated [at least on it's type_id/name]? 
	 * There are 3 cases:
	 * #1. The symbol remains the same [at least on unique index key]. We will 
	 *     perform only a simple update.
	 * #2. The symbol has changed: at least on name/type/file. We will insert a 
	 *     new symbol on table 'symbol'. Deletion of old one will take place 
	 *     at a second stage, when a delete of all symbols with 
	 *     'tmp_flag = 0' will be done.
	 * #3. The symbol has been deleted. As above it will be deleted at 
	 *     a second stage because of the 'tmp_flag = 0'. Triggers will remove 
	 *     also scope_ids and other things.
	 */
	
	if (update_flag == FALSE)	/* symbol is new */
	{
		symbol_id = -1;
	}
	else 						/* symbol is updated or a force_update has been given */
	{		
		/* We should use more value and set them with the same values because
		 * sdb_engine_get_tuple_id_by_unique_name () will manage them
	 	 */
		SDB_GVALUE_SET_STATIC_STRING(v1, name);
		SDB_GVALUE_SET_INT(v2, file_defined_id);
		SDB_GVALUE_SET_STATIC_STRING(v3, type_type);
		SDB_GVALUE_SET_STATIC_STRING(v4, type_name);
		SDB_GVALUE_SET_INT(v5, file_position);
		
		/* 
		 * We cannot live without this select because we must know whether a similar 
		 * symbol was already present in the file or not. With this information we
		 * can see if it's been updated or newly inserted 
		 */
		symbol_id = sdb_engine_get_tuple_id_by_unique_name5 (dbe,
							  PREP_QUERY_GET_SYMBOL_ID_BY_UNIQUE_INDEX_KEY_EXT,
							  "symname", &v1,
							  "filedefid", &v2,
							  "typetype", &v3,
		    				  "typename", &v4,
							  "fileposition", &v5);
	}
	
	/* ok then, parse the symbol id value */
	if (symbol_id <= 0)
	{
		/* case 2 and 3 */
		sym_was_updated = FALSE;		
		plist = NULL;
		stmt = NULL;		

		sdb_engine_add_new_symbol_case_2_3 (dbe, symbol_id, &plist, &stmt, 
    							  file_defined_id, name, type_type, type_name);
	}
	else
	{
		/* case 1 */
		sym_was_updated = TRUE;		
		plist = NULL;
		stmt = NULL;
		
		sdb_engine_add_new_symbol_case_1 (dbe, symbol_id, &plist, &stmt);
	}

	/* common params */
	sdb_engine_add_new_symbol_common_params (dbe,  plist, stmt,
    									  	 file_position, is_file_scope,
	    									 signature,	returntype, scope_definition_id,
    							  		 	 scope_id, kind_id,
    									 	 access_kind_id, implementation_kind_id,
    									 	 update_flag);
	
	/* execute the query with parameters just set */
	nrows = gda_connection_statement_execute_non_select (priv->db_connection, 
													 (GdaStatement*)stmt, 
													 (GdaSet*)plist, &last_inserted,
													 &error);
	
	if (error)
	{
		g_warning ("SQL execute_non_select failed: %s", error->message);
		g_error_free (error);
	}
	
	if (sym_was_updated == FALSE)
	{
		if (nrows > 0)
		{
			const GValue *value = gda_set_get_holder_value (last_inserted, "+0");
			table_id = g_value_get_int (value);			
		
			/* This is a wrong place to emit the symbol-updated signal. Infact
		 	 * db is in a inconsistent state, e.g. inheritance references are still
		 	 * *not* calculated.
		 	 * So add the symbol id into a queue that will be parsed once and emitted.
		 	 */		
			g_async_queue_push (priv->inserted_syms_id_aqueue, GINT_TO_POINTER(table_id));			
		}
		else
		{
			table_id = -1;
		}
	}
	else
	{
		if (nrows > 0)
		{
			table_id = symbol_id;
						
			g_async_queue_push (priv->updated_syms_id_aqueue, GINT_TO_POINTER(table_id));
		}
		else 
		{
			table_id = -1;
		}
	}
	
	if (last_inserted)
		g_object_unref (last_inserted);	

	/* post population phase */
	
	/* before returning the table_id we have to fill some infoz on temporary tables
	 * so that in a second pass we can parse also the heritage and scope fields.
	 */	
	if (table_id > 0)
		sdb_engine_add_new_tmp_heritage_scope (dbe, tag_entry, table_id);

	g_free (type_regex);
	
	return table_id;
}

/**
 * ### Thread note: this function inherits the mutex lock ### 
 *
 * Select * from __tmp_removed and emits removed signals.
 */
static void
sdb_engine_detects_removed_ids (SymbolDBEngine *dbe)
{
	const GdaStatement *stmt1, *stmt2;
	GdaDataModel *data_model;
	SymbolDBEnginePriv *priv;
	gint i, num_rows;	
		
	priv = dbe->priv;
	
	/* ok, now we should read from __tmp_removed all the symbol ids which have
	 * been removed, and emit a signal 
	 */
	if ((stmt1 = sdb_engine_get_statement_by_query_id (dbe,
											 PREP_QUERY_GET_REMOVED_IDS))
		== NULL)
	{
		g_warning ("query is null");
		return;
	}
	
	data_model = gda_connection_statement_execute_select (priv->db_connection, 
												   (GdaStatement*)stmt1, 
													NULL, NULL);
	
	if (GDA_IS_DATA_MODEL (data_model)) 
	{
		if ((num_rows = gda_data_model_get_n_rows (data_model)) <= 0)
		{
			DEBUG_PRINT ("nothing to remove");
			g_object_unref (data_model);
			return;
		}
	}
	else
	{
		if (data_model != NULL)
			g_object_unref (data_model);
		return;
	}

	/* get and parse the results. */
	for (i = 0; i < num_rows; i++) 
	{
		const GValue *val;
		gint tmp;
		val = gda_data_model_get_value_at (data_model, 0, i, NULL);
		tmp = g_value_get_int (val);
		DBESignal *dbesig1;
		DBESignal *dbesig2;

		dbesig1 = g_slice_new (DBESignal);
		dbesig1->value = GINT_TO_POINTER (SYMBOL_REMOVED + 1);
		dbesig1->process_id = priv->current_scan_process_id;

		dbesig2 = g_slice_new (DBESignal);
		dbesig2->value = GINT_TO_POINTER (tmp);
		dbesig2->process_id = priv->current_scan_process_id;
		
		g_async_queue_push (priv->signals_aqueue, dbesig1);
		g_async_queue_push (priv->signals_aqueue, dbesig2);
	}

	g_object_unref (data_model);
	
	/* let's clean the tmp_table */
	if ((stmt2 = sdb_engine_get_statement_by_query_id (dbe,
											 PREP_QUERY_TMP_REMOVED_DELETE_ALL))
		== NULL)
	{
		g_warning ("query is null");
		return;
	}

	/* bye bye */
	gda_connection_statement_execute_non_select (priv->db_connection, 
													 (GdaStatement*)stmt2, 
													 NULL, NULL,
													 NULL);
}

/**
 * ~~~ Thread note: this function locks the mutex ~~~ *
 *
 * WARNING: do not use this function thinking that it would do a scan of symbols
 * too. Use symbol_db_engine_update_files_symbols () instead. This one will set
 * up some things on db, like removing the 'old' symbols which have not been 
 * updated.
 */
static gboolean
sdb_engine_update_file (SymbolDBEngine * dbe, const gchar * file_on_db)
{
	const GdaSet *plist1, *plist2, *plist3;
	const GdaStatement *stmt1, *stmt2, *stmt3;
	GdaHolder *param;
	SymbolDBEnginePriv *priv;
	GValue v = {0};

	priv = dbe->priv;

	SDB_LOCK(priv);
	
	/* if we're updating symbols we must do some other operations on db 
	 * symbols, like remove the ones which don't have an update_flag = 1 
	 * per updated file.
	 */

	/* good. Go on with removing of old symbols, marked by a 
	 * update_flag = 0. 
	 */

	/* Triggers will take care of updating/deleting connected symbols
	 * tuples, like sym_kind, sym_type etc */
	if ((stmt1 = sdb_engine_get_statement_by_query_id (dbe, 
									PREP_QUERY_REMOVE_NON_UPDATED_SYMBOLS)) == NULL)
	{
		g_warning ("query is null");
		SDB_UNLOCK(priv);
		
		return FALSE;
	}

	plist1 = sdb_engine_get_query_parameters_list (dbe, PREP_QUERY_REMOVE_NON_UPDATED_SYMBOLS);
	
	if ((param = gda_set_get_holder ((GdaSet*)plist1, "filepath")) == NULL)
	{
		g_warning ("param filepath is NULL from pquery!");
		SDB_UNLOCK(priv);
		return FALSE;
	}
	
	SDB_PARAM_SET_STRING(param, file_on_db);
	
	gda_connection_statement_execute_non_select (priv->db_connection, (GdaStatement*)stmt1, 
														 (GdaSet*)plist1, NULL, NULL);	

	/* emits removed symbols signals */
	sdb_engine_detects_removed_ids (dbe);

	/* reset the update_flag to 0 */	
	if ((stmt2 = sdb_engine_get_statement_by_query_id (dbe, 
									PREP_QUERY_RESET_UPDATE_FLAG_SYMBOLS)) == NULL)
	{
		g_warning ("query is null");
		SDB_UNLOCK(priv);
		return FALSE;
	}
	
	plist2 = sdb_engine_get_query_parameters_list (dbe, PREP_QUERY_RESET_UPDATE_FLAG_SYMBOLS);
	
	if ((param = gda_set_get_holder ((GdaSet*)plist2, "filepath")) == NULL)
	{
		g_warning ("param filepath is NULL from pquery!");
		return FALSE;
	}
	SDB_PARAM_SET_STRING(param, file_on_db);
	
	gda_connection_statement_execute_non_select (priv->db_connection, (GdaStatement*)stmt2, 
														 (GdaSet*)plist2, NULL, NULL);	

	/* last but not least, update the file analyse_time */
	if ((stmt3 = sdb_engine_get_statement_by_query_id (dbe,
											 PREP_QUERY_UPDATE_FILE_ANALYSE_TIME))
		== NULL)
	{
		g_warning ("query is null");
		SDB_UNLOCK(priv);
		return FALSE;
	}

	plist3 = sdb_engine_get_query_parameters_list (dbe, PREP_QUERY_UPDATE_FILE_ANALYSE_TIME);
	
	/* filepath parameter */
	if ((param = gda_set_get_holder ((GdaSet*)plist3, "filepath")) == NULL)
	{
		g_warning ("param filepath is NULL from pquery!");
		SDB_UNLOCK(priv);
		return FALSE;
	}
	
	SDB_PARAM_SET_STRING(param, file_on_db);

	gda_connection_statement_execute_non_select (priv->db_connection, (GdaStatement*)stmt3, 
														 (GdaSet*)plist3, NULL, NULL);	

	SDB_UNLOCK(priv);
	return TRUE;
}

/**
 * @param data is a GPtrArray *files_to_scan
 * It will be freed when this callback will be called.
 */
static void
on_scan_update_files_symbols_end (SymbolDBEngine * dbe, 
								  gint process_id,
								  UpdateFileSymbolsData* update_data)
{
	SymbolDBEnginePriv *priv;
	GPtrArray *files_to_scan;
	gint i;
	GValue v = {0};

	g_return_if_fail (dbe != NULL);
	g_return_if_fail (update_data != NULL);
	
	priv = dbe->priv;
	files_to_scan = update_data->files_path;
	
	sdb_engine_clear_caches (dbe);
	
	/* we need a reinitialization */
	sdb_engine_init_caches (dbe);
	
	for (i = 0; i < files_to_scan->len; i++)
	{
		gchar *node = (gchar *) g_ptr_array_index (files_to_scan, i);
		
		if (strstr (node, update_data->project_directory) == NULL) 
		{
			g_warning ("node %s is shorter than "
					   "prj_directory %s",
					   node, update_data->project_directory);
			continue;
		}
		
		/* clean the db from old un-updated with the last update step () */
		if (sdb_engine_update_file (dbe, node + 
									strlen (update_data->project_directory)) == FALSE)
		{
			g_warning ("Error processing file %s", node + 
					   strlen (update_data->project_directory));
			return;
		}
	}
		
	g_signal_handlers_disconnect_by_func (dbe, on_scan_update_files_symbols_end,
										  update_data);

	/* if true, we'll update the project scanning time too. 
	 * warning: project time scanning won't could be set before files one.
	 * This why we'll fork the process calling sdb_engine_scan_files ()
	 */
	if (update_data->update_prj_analyse_time == TRUE)
	{
		const GdaSet *plist;
		const GdaStatement *stmt;
		GdaHolder *param;

		SDB_LOCK(priv);
		/* and the project analyse_time */
		if ((stmt = sdb_engine_get_statement_by_query_id (dbe,
									PREP_QUERY_UPDATE_PROJECT_ANALYSE_TIME))
			== NULL)
		{
			g_warning ("query is null");
			SDB_UNLOCK(priv);
			return;
		}

		plist = sdb_engine_get_query_parameters_list (dbe, PREP_QUERY_UPDATE_PROJECT_ANALYSE_TIME);
			
		/* prjname parameter */
		if ((param = gda_set_get_holder ((GdaSet*)plist, "prjname")) == NULL)
		{
			g_warning ("param prjname is NULL from pquery!");
			SDB_UNLOCK(priv);
			return;
		}
		
		SDB_PARAM_SET_STRING(param, update_data->project);
	
		gda_connection_statement_execute_non_select (priv->db_connection, 
													 (GdaStatement*)stmt, 
													 (GdaSet*)plist, NULL, NULL);

		SDB_UNLOCK(priv);
	}	
	
	/* free the GPtrArray. */
	g_ptr_array_unref (files_to_scan);

	g_free (update_data->project);
	g_free (update_data->project_directory);
	g_free (update_data);
}

/**
 * symbol_db_engine_update_files_symbols:
 * @dbe: self
 * @project: name of the project
 * @files_path: absolute path of files to update.
 * @update_prj_analyse_time: flag to force the update of project analyse time.
 * 
 * Update symbols of saved files. 
 * 
 * Returns: Scan process id if insertion is successful, -1 on 'no files scanned'.
 */
gint
symbol_db_engine_update_files_symbols (SymbolDBEngine * dbe, const gchar * project, 
									   const GPtrArray * files_path,
									   gboolean update_prj_analyse_time)
{
	SymbolDBEnginePriv *priv;
	UpdateFileSymbolsData *update_data;
	gboolean ret_code;
	gint ret_id, scan_id;
	gint i;
	GPtrArray * ready_files;
	
	priv = dbe->priv;

	g_return_val_if_fail (priv->db_connection != NULL, FALSE);
	g_return_val_if_fail (project != NULL, FALSE);

	ready_files = g_ptr_array_new_with_free_func (g_free);
	
	/* check if the files exist in db before passing them to the scan procedure */
	for (i = 0; i < files_path->len; i++) 
	{
		gchar *curr_abs_file;
		
		curr_abs_file = g_strdup (g_ptr_array_index (files_path, i));
		/* check if the file exists in db. We will not scan buffers for files
		 * which aren't already in db
		 */
		if (symbol_db_engine_file_exists (dbe, curr_abs_file) == FALSE)
		{
			DEBUG_PRINT ("will not update file symbols claiming to be %s because not in db",
						 curr_abs_file);
			
			g_free (curr_abs_file);
			continue;
		}
		
		/* ok the file exists in db. Add it to ready_files */
		g_ptr_array_add (ready_files, curr_abs_file);
	}
		
	/* if no file has been added to the array then bail out here */
	if (ready_files->len <= 0)
	{
		g_ptr_array_unref (ready_files);
		DEBUG_PRINT ("not enough files to update");
		return -1;
	}
	
	update_data = g_new0 (UpdateFileSymbolsData, 1);
	
	update_data->update_prj_analyse_time = update_prj_analyse_time;
	update_data->files_path = ready_files;
	update_data->project = g_strdup (project);
	update_data->project_directory = g_strdup (priv->project_directory);

	
	/* data will be freed when callback will be called. The signal will be
	 * disconnected too, don't worry about disconneting it by hand.
	 */
	g_signal_connect (G_OBJECT (dbe), "scan-end",
					  G_CALLBACK (on_scan_update_files_symbols_end), update_data);

	scan_id = sdb_engine_get_unique_scan_id (dbe);
	ret_code = sdb_engine_scan_files_async (dbe, ready_files, NULL, TRUE, scan_id);
	
	if (ret_code == TRUE)
	{
		ret_id = scan_id;
	}
	else
		ret_id = -1;
	
	return ret_id;
}

/**
 * symbol_db_engine_update_project_symbols:
 * @dbe: self
 * @project_name: The project name
 * @force_all_files: 
 * 
 * Update symbols of the whole project. It scans all file symbols etc. 
 * If force is true then update forcely all the files.
 * ~~~ Thread note: this function locks the mutex ~~~ *
 * 
 * Returns: scan id of the process, or -1 in case of problems.
 */
gint
symbol_db_engine_update_project_symbols (SymbolDBEngine *dbe, 
    		const gchar *project_name, gboolean force_all_files)
{
	const GdaSet *plist;
	const GdaStatement *stmt;
	GdaHolder *param;
	GdaDataModel *data_model;
	gint num_rows = 0;
	gint i;
	GPtrArray *files_to_scan;
	SymbolDBEnginePriv *priv;
	GValue v = {0};
	
	g_return_val_if_fail (dbe != NULL, FALSE);
	
	priv = dbe->priv;
	
	g_return_val_if_fail (project_name != NULL, FALSE);
	g_return_val_if_fail (priv->project_directory != NULL, FALSE);
	
	SDB_LOCK(priv);

	DEBUG_PRINT ("Updating symbols of project-name %s (force %d)...", project_name, force_all_files);
	if ((stmt = sdb_engine_get_statement_by_query_id (dbe,
								 PREP_QUERY_GET_ALL_FROM_FILE_BY_PROJECT_NAME))
		== NULL)
	{
		g_warning ("query is null");
		SDB_UNLOCK(priv);
		return FALSE;
	}

	plist = sdb_engine_get_query_parameters_list (dbe, 
								PREP_QUERY_GET_ALL_FROM_FILE_BY_PROJECT_NAME);

	/* prjname parameter */
	if ((param = gda_set_get_holder ((GdaSet*)plist, "prjname")) == NULL)
	{
		g_warning ("param prjid is NULL from pquery!");
		SDB_UNLOCK(priv);		
		return FALSE;
	}

	SDB_PARAM_SET_STRING(param, project_name);	
	
	/* execute the query with parameters just set */
	GType gtype_array [6] = {	G_TYPE_INT, 
								G_TYPE_STRING, 
								G_TYPE_INT, 
								G_TYPE_INT, 
#ifdef HAVE_GDA6
								G_TYPE_DATE_TIME,
#else
								GDA_TYPE_TIMESTAMP, 
#endif
								G_TYPE_NONE
							};
	data_model = gda_connection_statement_execute_select_full (priv->db_connection, 
												(GdaStatement*)stmt, 
	    										(GdaSet*)plist,
	    										GDA_STATEMENT_MODEL_RANDOM_ACCESS,
	    										gtype_array,
	    										NULL);
	
	if (!GDA_IS_DATA_MODEL (data_model) ||
		(num_rows = gda_data_model_get_n_rows (GDA_DATA_MODEL (data_model))) <= 0)
	{
		if (data_model != NULL)
			g_object_unref (data_model);
		data_model = NULL;

		g_warning ("Strange enough, no files in project ->%s<- found",
		    project_name);
		SDB_UNLOCK(priv);		
		return FALSE;		    
	}

	/* initialize the array */
	files_to_scan = g_ptr_array_new_with_free_func (g_free);

	/* we can now scan each filename entry to check the last modification time. */
	for (i = 0; i < num_rows; i++)
	{	
		const GValue *value, *value1;
#ifdef HAVE_GDA6
		GDateTime *timestamp;
#else
		const GdaTimestamp *timestamp;
#endif
		const gchar *file_name;
		gchar *file_abs_path = NULL;
		struct tm filetm;
		time_t db_time;
		GFile *gfile;
		GFileInfo* gfile_info;
		GFileInputStream* gfile_is;

		if ((value =
			 gda_data_model_get_value_at (data_model, 
						gda_data_model_get_column_index(data_model,
												   "db_file_path"), 
										  i, NULL)) == NULL)
		{
			continue;
		}

		/* build abs path. */
		file_name = g_value_get_string (value);
		if (!file_name)
			continue;

		file_abs_path = g_build_filename (priv->project_directory,
		    file_name, NULL);

		gfile = g_file_new_for_path (file_abs_path);
		gfile_is = g_file_read (gfile, NULL, NULL);
		/* retrieve data/time info */
		if (gfile_is == NULL)
		{
			g_message ("could not open path %s", file_abs_path);
			g_free (file_abs_path);
			g_object_unref (gfile);
			continue;
		}
		g_object_unref (gfile_is);
				
		gfile_info = g_file_query_info (gfile, "*", G_FILE_QUERY_INFO_NONE,
										NULL, NULL);

		if (gfile_info == NULL)
		{
			g_message ("cannot get file info from handle");
			g_free (file_abs_path);
			g_object_unref (gfile);
			continue;
		}

		if ((value1 = gda_data_model_get_value_at (data_model, 
						gda_data_model_get_column_index(data_model,
												   "analyse_time"), i, NULL)) == NULL)
		{
			continue;
		}


#ifdef HAVE_GDA6
		timestamp = g_value_get_boxed (value1);
#else
		timestamp = gda_value_get_timestamp (value1);
#endif

		/* fill a struct tm with the date retrieved by the string. */
		/* string is something like '2007-04-18 23:51:39' */
		memset (&filetm, 0, sizeof (struct tm));
#ifdef HAVE_GDA6
		filetm.tm_year = g_date_time_get_year (timestamp) - 1900;
		filetm.tm_mon = g_date_time_get_month (timestamp) - 1;
		filetm.tm_mday = g_date_time_get_day_of_month (timestamp);
		filetm.tm_hour = g_date_time_get_hour (timestamp);
		filetm.tm_min = g_date_time_get_minute (timestamp);
		filetm.tm_sec = g_date_time_get_second (timestamp);
#else
		filetm.tm_year = timestamp->year - 1900;		
		filetm.tm_mon = timestamp->month - 1;
		filetm.tm_mday = timestamp->day;
		filetm.tm_hour = timestamp->hour;
		filetm.tm_min = timestamp->minute;
		filetm.tm_sec = timestamp->second;
#endif

		/* remove one hour to the db_file_time. */
		db_time = mktime (&filetm) - 3600;

		guint64 modified_time = g_file_info_get_attribute_uint64 (gfile_info, 
										  G_FILE_ATTRIBUTE_TIME_MODIFIED);
		if (difftime (db_time, modified_time) < 0 ||
		    force_all_files == TRUE)
		{
			g_ptr_array_add (files_to_scan, file_abs_path);
		}
		else
		{
			g_free (file_abs_path);
		}
		
		g_object_unref (gfile_info);
		g_object_unref (gfile);
	}
	
	if (data_model)
		g_object_unref (data_model);
	
	if (files_to_scan->len > 0)
	{
		SDB_UNLOCK(priv);

		/* at the end let the scanning function do its job */
		gint id = symbol_db_engine_update_files_symbols (dbe, project_name,
											   files_to_scan, TRUE);

		g_ptr_array_unref (files_to_scan);
		return id;
	}
	
	SDB_UNLOCK(priv);

	/* some error occurred */
	return -1;
}

/** 
 * symbol_db_engine_remove_file:
 * @dbe: self
 * @project: project name
 * @rel_file: db relative file entry of the symbols to remove.
 * 
 * Remove a file, together with its symbols, from a project. I.e. it won't remove
 * physically the file from disk.
 * ~~~ Thread note: this function locks the mutex
 * 
 * Returns: TRUE if everything went good, FALSE otherwise.
 */
gboolean
symbol_db_engine_remove_file (SymbolDBEngine * dbe, const gchar *project,
                              const gchar *rel_file)
{
	SymbolDBEnginePriv *priv;	
	const GdaSet *plist;
	const GdaStatement *stmt;
	GdaHolder *param;
	GValue v = {0};

	
	g_return_val_if_fail (dbe != NULL, FALSE);
	g_return_val_if_fail (project != NULL, FALSE);
	g_return_val_if_fail (rel_file != NULL, FALSE);
	priv = dbe->priv;
	
	SDB_LOCK(priv);

	if (strlen (rel_file) <= 0)
	{
		g_warning ("wrong file to delete.");
		SDB_UNLOCK(priv);
		return FALSE;
	}
	
	DEBUG_PRINT ("deleting from db %s", rel_file);
	
	if ((stmt = sdb_engine_get_statement_by_query_id (dbe, 
									PREP_QUERY_REMOVE_FILE_BY_PROJECT_NAME)) == NULL)
	{
		g_warning ("query is null");
		SDB_UNLOCK(priv);
		return FALSE;
	}

	plist = sdb_engine_get_query_parameters_list (dbe, PREP_QUERY_REMOVE_FILE_BY_PROJECT_NAME);

	if ((param = gda_set_get_holder ((GdaSet*)plist, "prjname")) == NULL)
	{
		g_warning ("param prjname is NULL from pquery!");
		SDB_UNLOCK(priv);
		return FALSE;
	}

	SDB_PARAM_SET_STRING(param, project);
	
	if ((param = gda_set_get_holder ((GdaSet*)plist, "filepath")) == NULL)
	{
		g_warning ("param filepath is NULL from pquery!");
		SDB_UNLOCK(priv);
		return FALSE;
	}
	
	SDB_PARAM_SET_STRING(param, rel_file);	

	/* Triggers will take care of updating/deleting connected symbols
	 * tuples, like sym_kind, sym_type etc */	
	gda_connection_statement_execute_non_select (priv->db_connection, (GdaStatement*)stmt, 
														 (GdaSet*)plist, NULL, NULL);

	/* emits removed symbols signals */
	sdb_engine_detects_removed_ids (dbe);
	
	SDB_UNLOCK(priv);
	
	return TRUE;
}

void
symbol_db_engine_remove_files (SymbolDBEngine * dbe, const gchar * project,
                                                         const GPtrArray * files)
{
	gint i;
	
	g_return_if_fail (dbe != NULL);
	g_return_if_fail (project != NULL);
	g_return_if_fail (files != NULL);

	for (i = 0; i < files->len; i++)
	{
		symbol_db_engine_remove_file (dbe, project, g_ptr_array_index (files, i));
	}	
}

static void
on_scan_update_buffer_end (SymbolDBEngine * dbe, gint process_id, gpointer data)
{
	GPtrArray *files_to_scan;
	gint i;

	g_return_if_fail (dbe != NULL);
	g_return_if_fail (data != NULL);

	files_to_scan = (GPtrArray *) data;

	for (i = 0; i < files_to_scan->len; i++)
	{
		gchar *node = (gchar *) g_ptr_array_index (files_to_scan, i);
		const gchar *relative_path = symbol_db_util_get_file_db_path (dbe, node);
		if (relative_path != NULL)
		{
			/* will be emitted removed signals */
			if (sdb_engine_update_file (dbe, relative_path) == FALSE)
			{
				g_warning ("Error processing file %s", node);
				return;
			}
		}
	}
		
	g_signal_handlers_disconnect_by_func (dbe, on_scan_update_buffer_end,
										  files_to_scan);

	/* free the GPtrArray. */
	g_ptr_array_unref (files_to_scan);
	data = files_to_scan = NULL;
}

/**
 * symbol_db_engine_update_buffer_symbols:
 * @dbe: self
 * @project: project name
 * @real_files: full path on disk to 'real file' to update. e.g.
 * 				/home/foouser/fooproject/src/main.c. 
 * @text_buffers: memory buffers
 * @buffer_sizes: one to one sizes with text_buffers.
 * 
 * Update symbols of a file by a memory-buffer to perform a real-time updating 
 * of symbols. 
 * 
 * Returns: scan process id if insertion is successful, -1 on error.
 */
gint
symbol_db_engine_update_buffer_symbols (SymbolDBEngine * dbe, const gchar *project,
										const GPtrArray * real_files,
										const GPtrArray * text_buffers,
										const GPtrArray * buffer_sizes)
{
	SymbolDBEnginePriv *priv;
	gint i;
	gint ret_id, scan_id;
	gboolean ret_code;
	/* array that'll represent the /dev/shm/anjuta-XYZ files */
	GPtrArray *temp_files;
	GPtrArray *real_files_list;
	GPtrArray *real_files_on_db;
	
	g_return_val_if_fail (dbe != NULL, FALSE);
	priv = dbe->priv;
	
	g_return_val_if_fail (priv->db_connection != NULL, FALSE);
	g_return_val_if_fail (project != NULL, FALSE);
	g_return_val_if_fail (real_files != NULL, FALSE);
	g_return_val_if_fail (text_buffers != NULL, FALSE);
	g_return_val_if_fail (buffer_sizes != NULL, FALSE);
	
	temp_files = g_ptr_array_new_with_free_func (g_free);	
	real_files_on_db = g_ptr_array_new_with_free_func (g_free);
	real_files_list = anjuta_util_clone_string_gptrarray (real_files);
	
	/* obtain a GPtrArray with real_files on database */
	for (i=0; i < real_files_list->len; i++) 
	{
		const gchar *relative_path;
		const gchar *curr_abs_file;
		FILE *buffer_mem_file;
		const gchar *temp_buffer;
		gint buffer_mem_fd;
		gint temp_size;
		gchar *shared_temp_file;
		gchar *base_filename;
		
		curr_abs_file = g_ptr_array_index (real_files_list, i);
		/* check if the file exists in db. We will not scan buffers for files
		 * which aren't already in db
		 */
		if (symbol_db_engine_file_exists (dbe, curr_abs_file) == FALSE)
		{
			DEBUG_PRINT ("will not scan buffer claiming to be %s because not in db",
						 curr_abs_file);
			continue;
		}		
		
		relative_path = g_strdup (symbol_db_util_get_file_db_path (dbe, curr_abs_file));
		if (relative_path == NULL)
		{
			g_warning ("relative_path is NULL");
			continue;
		}
		g_ptr_array_add (real_files_on_db, (gpointer) relative_path);

		/* it's ok to have just the base filename to create the
		 * target buffer one */
		base_filename = g_filename_display_basename (relative_path);
		
		shared_temp_file = g_strdup_printf ("/anjuta-%d-%ld-%s", getpid (),
						 time (NULL), base_filename);
		g_free (base_filename);
		
		if ((buffer_mem_fd = 
			 shm_open (shared_temp_file, O_CREAT|O_RDWR, S_IRUSR|S_IWUSR)) < 0)
		{
			g_warning ("Error while trying to open a shared memory file. Be"
					   "sure to have "SHARED_MEMORY_PREFIX" mounted with tmpfs");
			return -1;
		}
	
		buffer_mem_file = fdopen (buffer_mem_fd, "w+b");
		
		temp_buffer = g_ptr_array_index (text_buffers, i);
		temp_size = GPOINTER_TO_INT(g_ptr_array_index (buffer_sizes, i));
		
		fwrite (temp_buffer, sizeof(gchar), temp_size, buffer_mem_file);
		fflush (buffer_mem_file);
		fclose (buffer_mem_file);
		
		/* add the temp file to the array. */
		g_ptr_array_add (temp_files, g_strdup_printf (SHARED_MEMORY_PREFIX"%s", 
													  shared_temp_file));
		
		/* check if we already have an entry stored in the hash table, else
		 * insert it 
		 */		
		if (g_hash_table_lookup (priv->garbage_shared_mem_files, shared_temp_file) 
			== NULL)
		{
			DEBUG_PRINT ("inserting into garbage hash table %s", shared_temp_file);
			g_hash_table_insert (priv->garbage_shared_mem_files, shared_temp_file, 
								 NULL);
		}
		else 
		{
			/* the item is already stored. Just free it here. */
			g_free (shared_temp_file);
		}
	}

	/* in case we didn't have any good buffer to scan...*/
	ret_id = -1;
	
	/* it may happen that no buffer is correctly set up */
	if (real_files_on_db->len > 0)
	{
		/* data will be freed when callback will be called. The signal will be
	 	 * disconnected too, don't worry about disconnecting it by hand.
	 	 */
		g_signal_connect (G_OBJECT (dbe), "scan-end",
						  G_CALLBACK (on_scan_update_buffer_end), real_files_list);

		scan_id = sdb_engine_get_unique_scan_id (dbe);		
		ret_code = sdb_engine_scan_files_async (dbe, temp_files, real_files_on_db, TRUE, scan_id);
		
		if (ret_code == TRUE)
		{
			ret_id = scan_id;
		}
		else
			ret_id = -1;
	}	
	
	g_ptr_array_unref (temp_files);	
	g_ptr_array_unref (real_files_on_db);
	return ret_id;
}

/**
 * symbol_db_engine_get_files_for_project:
 * @dbe: self
 *  
 * Retrieves the list of files in project. The data model contains only 1
 * column, which is the file name.
 * 
 * Returns: data model which must be freed once used.
 */
GdaDataModel*
symbol_db_engine_get_files_for_project (SymbolDBEngine *dbe)
{
	return sdb_engine_execute_select_sql (dbe, "SELECT file.file_path FROM file");
}

/**
 * symbol_db_engine_set_db_case_sensitive:
 * @dbe: self
 * @case_sensitive: boolean flag.
 * 
 * Set the opened db case sensitive. The searches on this db will then be performed
 * taking into consideration this SQLite's PRAGMA case_sensitive_like.
 * ~~~ Thread note: this function locks the mutex ~~~
 * 
 */
void
symbol_db_engine_set_db_case_sensitive (SymbolDBEngine *dbe, gboolean case_sensitive)
{
	g_return_if_fail (dbe != NULL);

	if (case_sensitive == TRUE)
		sdb_engine_execute_unknown_sql (dbe, "PRAGMA case_sensitive_like = 1");
	else 
		sdb_engine_execute_unknown_sql (dbe, "PRAGMA case_sensitive_like = 0");
}

/**
 * symbol_db_engine_get_type_conversion_hash:
 * @dbe: self
 * 
 * Get conversion hash table used to convert symbol type name to enum value
 * 
 * Returns: a GHashTable which must be freed once used.
 */
const GHashTable*
symbol_db_engine_get_type_conversion_hash (SymbolDBEngine *dbe)
{
	g_return_val_if_fail (SYMBOL_IS_DB_ENGINE (dbe), NULL);
	return dbe->priv->sym_type_conversion_hash;
}

/**
 * symbol_db_engine_get_project_directory:
 * @dbe: self
 * 
 * Gets the project directory (used to construct absolute paths)
 *
 * Returns: a const gchar containing the project_directory.
 */
const gchar*
symbol_db_engine_get_project_directory (SymbolDBEngine *dbe)
{
	g_return_val_if_fail (SYMBOL_IS_DB_ENGINE (dbe), NULL);
	return dbe->priv->project_directory;
}

/**
 * symbol_db_engine_get_statement:
 * @dbe: self
 * @sql_str: sql statement.
 * 
 * Compiles an sql statement
 * 
 * Returns: a GdaStatement object which must be freed once used.
 */
GdaStatement*
symbol_db_engine_get_statement (SymbolDBEngine *dbe, const gchar *sql_str)
{
	GdaStatement* stmt;
	GError *error = NULL;
	
	g_return_val_if_fail (SYMBOL_IS_DB_ENGINE (dbe), NULL);
	stmt = gda_sql_parser_parse_string (dbe->priv->sql_parser,
	                                    sql_str,
	                                    NULL, &error);
	if (error)
	{
		g_warning ("SQL parsing failed: %s: %s", sql_str, error->message);
		g_error_free (error);
	}
	return stmt;
}

/**
 * symbol_db_engine_execute_select:
 * @dbe: self
 * @stmt: A compiled GdaStatement sql statement.
 * @params: Params for GdaStatement (i.e. a prepared statement).
 * 
 * Executes a parameterized sql statement
 * 
 * Returns: A data model which must be freed once used.
 */
GdaDataModel*
symbol_db_engine_execute_select (SymbolDBEngine *dbe, GdaStatement *stmt,
                                 GdaSet *params)
{
	GdaDataModel *res;
	GError *error = NULL;
	
	res = gda_connection_statement_execute_select (dbe->priv->db_connection, 
												   stmt, params, &error);
	if (error)
	{
		gchar *sql_str =
			gda_statement_to_sql_extended (stmt, dbe->priv->db_connection,
			                               params, 0, NULL, NULL);

		g_warning ("SQL select exec failed: %s, %s", sql_str, error->message);
		g_free (sql_str);
		g_error_free (error);
	}
	return res;
}
