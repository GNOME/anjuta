/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* gbf-tree-data.c
 *
 * Copyright (C) 2000  JP Rosevear
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 *
 * Author: JP Rosevear
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <gio/gio.h>
#include "gbf-tree-data.h"

GType
gbf_tree_data_get_type (void)
{
    static GType our_type = 0;
  
    if (our_type == 0)
        our_type = g_boxed_type_register_static ("GbfProjectTreeNodeData",
                                                 (GBoxedCopyFunc) gbf_tree_data_copy,
                                                 (GBoxedFreeFunc) gbf_tree_data_free);

    return our_type;
}

GbfTreeData *
gbf_tree_data_new_string (const gchar *string)
{
	GbfTreeData *node = g_new0 (GbfTreeData, 1);
	
	node->type = GBF_TREE_NODE_STRING;
	node->name = g_strdup (string);
	
	return node;
}

GbfTreeData *
gbf_tree_data_new_group (GbfProject *project, const GbfProjectGroup *group)
{
	GbfTreeData *node = g_new0 (GbfTreeData, 1);
	
	node->type = GBF_TREE_NODE_GROUP;
	node->name = g_strdup (group->name);
	node->id = g_strdup (group->id);

	return node;
}

GbfTreeData *
gbf_tree_data_new_target (GbfProject *project, const GbfProjectTarget *target)
{
	GbfTreeData *node = g_new0 (GbfTreeData, 1);
	
	node->type = GBF_TREE_NODE_TARGET;
	node->name = g_strdup (target->name);
	node->id = g_strdup (target->id);
	node->mime_type = g_strdup (gbf_project_mimetype_for_type (project, target->type));
	
	return node;
}

GbfTreeData *
gbf_tree_data_new_source (GbfProject *project, const GbfProjectTargetSource *source)
{
	GbfTreeData *node = g_new0 (GbfTreeData, 1);
	GFile *file;
	GFileInfo *file_info;
	
	node->type = GBF_TREE_NODE_TARGET_SOURCE;
	node->id = g_strdup (source->id);
	node->uri = g_strdup (source->source_uri);
	node->name = NULL;
	
	file = g_file_new_for_uri (source->source_uri);
	node->name = g_file_get_basename (file);
	if (g_file_query_exists (file, NULL))
	{
		file_info = g_file_query_info (file, 
			G_FILE_ATTRIBUTE_STANDARD_DISPLAY_NAME,
			G_FILE_QUERY_INFO_NONE,
			NULL, NULL);
		if (file_info)
		{
			node->name = g_strdup (g_file_info_get_display_name (file_info));
		}
	}
	g_object_unref (file);

	if (node->name == NULL)
	{
		node->name = g_file_get_basename (file);
	}

	return node;
}

GbfTreeData *
gbf_tree_data_copy (GbfTreeData *src)
{
	GbfTreeData *node;

	node = g_new (GbfTreeData, 1);
	node->type = src->type;
	node->name = g_strdup (src->name);
	node->id = g_strdup (src->id);
	node->uri = g_strdup (src->uri);
	node->is_shortcut = src->is_shortcut;
	node->mime_type = g_strdup (src->mime_type);
	
	return node;
}

void
gbf_tree_data_free (GbfTreeData *node)
{
	if (node) {
		g_free (node->name);
		g_free (node->id);
		g_free (node->uri);
		g_free (node->mime_type);
		g_free (node);
	}
}