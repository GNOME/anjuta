/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 4; tab-width: 4; coding: utf-8 -*- */
/* amp-module.c
 *
 * Copyright (C) 2009  Sébastien Granjoux
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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 *
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "amp-module.h"

#include "amp-node.h"
#include "am-scanner.h"
#include "am-properties.h"


#include <libanjuta/interfaces/ianjuta-project.h>

#include <libanjuta/anjuta-debug.h>

#include <glib/gi18n.h>

#include <memory.h>
#include <string.h>
#include <ctype.h>

/* Types
 *---------------------------------------------------------------------------*/

struct _AmpModuleNode {
	AnjutaProjectNode base;
	AnjutaToken *module;
};


/* Module objects
 *---------------------------------------------------------------------------*/

void
amp_module_node_add_token (AmpModuleNode *module, AnjutaToken *token)
{
	gchar *name;
	
	module->module = token;
	name = anjuta_token_evaluate (anjuta_token_first_item (token));
	if (name != NULL)
	{
		g_free (module->base.name);
		module->base.name = name;
	}
}

AnjutaToken *
amp_module_node_get_token (AmpModuleNode *node)
{
	return node->module;
}

void
amp_module_node_update_node (AmpModuleNode *node, AmpModuleNode *new_node)
{
	node->module = new_node->module;
}

AmpModuleNode*
amp_module_node_new (const gchar *name, GError **error)
{
	AmpModuleNode *module = NULL;

	module = g_object_new (AMP_TYPE_MODULE_NODE, NULL);
	module->base.name = g_strdup (name);;

	return module;
}

void
amp_module_node_free (AmpModuleNode *node)
{
	g_object_unref (G_OBJECT (node));
}


/* GObjet implementation
 *---------------------------------------------------------------------------*/

typedef struct _AmpModuleNodeClass AmpModuleNodeClass;

struct _AmpModuleNodeClass {
	AmpNodeClass parent_class;
};

G_DEFINE_DYNAMIC_TYPE (AmpModuleNode, amp_module_node, AMP_TYPE_NODE);

static void
amp_module_node_init (AmpModuleNode *node)
{
	node->base.type = ANJUTA_PROJECT_MODULE;
	node->base.native_properties = amp_get_module_property_list();
	node->base.state = ANJUTA_PROJECT_CAN_ADD_PACKAGE |
						ANJUTA_PROJECT_CAN_REMOVE;
	node->module = NULL;
}

static void
amp_module_node_finalize (GObject *object)
{
	AmpModuleNode *module = AMP_MODULE_NODE (object);

	g_list_foreach (module->base.custom_properties, (GFunc)amp_property_free, NULL);
	
	G_OBJECT_CLASS (amp_module_node_parent_class)->finalize (object);
}

static void
amp_module_node_class_init (AmpModuleNodeClass *klass)
{
	GObjectClass* object_class = G_OBJECT_CLASS (klass);
	
	object_class->finalize = amp_module_node_finalize;
}

static void
amp_module_node_class_finalize (AmpModuleNodeClass *klass)
{
}

void
amp_module_node_register (GTypeModule *module)
{
	amp_module_node_register_type (module);
}