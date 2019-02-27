/*
 * vala.c
 *
 * Copyright 2008 Abderrahim Kitouni <a.kitouni@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA 02110-1301, USA.
 */

/*
 *   INCLUDE FILES
 */
#include "general.h"  /* must always come first */
#include "parse.h"
#include "read.h"

#include "entry.h"
#include "ctags-vala.h"

CTagsVisitor *visitor;
/* using different data structure because fpos_t isn't available in Vala*/
static void make_ctags_entry (CTagsEntry* entry) {
	tagEntryInfo tag;
	initTagEntry(&tag, entry->name);

	tag.lineNumberEntry = TRUE;
	tag.lineNumber = entry->line_number;
	tag.kindName = entry->kind_name;
	tag.kind = entry->kind;
	tag.filePosition = entry->filePosition;
	tag.extensionFields.access = entry->access;
	tag.extensionFields.implementation = entry->implementation;
	tag.extensionFields.inheritance = entry->inheritance;
	tag.extensionFields.scope[0] = entry->scope[0];
	tag.extensionFields.scope[1] = entry->scope[1];
	tag.extensionFields.typeRef[0] = entry->typeref;
	tag.extensionFields.returnType = entry->returntype;
	tag.extensionFields.signature = entry->signature;
	makeTagEntry(&tag);
}

static kindOption ValaKinds [] = {
	{ TRUE,	'c', "class", "Classes" },
	{ TRUE, 's', "struct", "Structures" },
	{ TRUE, 'i', "interface", "Interfaces" },
	{ TRUE, 'e', "enum", "Enumerations" },
	{ TRUE, 'v', "enumvalue", "Enumeration Values" },
	{ TRUE, 'E', "errordomain", "Error domains" },
	{ TRUE, 'r', "errorcode", "Error codes" },
	{ TRUE, 'd', "delegate", "Delegates" },
	{ TRUE, 'S', "signal", "Signals" },
	{ TRUE,	'f', "field", "Fields" },
	{ TRUE,	'p', "property", "Properties" },
	{ TRUE, 'm', "method", "Methods" },
	{ FALSE, 'l', "local", "Local variables" },
};

static void findValaTags (void) {
	if (visitor == NULL) {
		visitor = ctags_visitor_new();
	}
	ctags_visitor_parse_vala (visitor, getSourceFileName(), (CTagsEntryMaker) make_ctags_entry);
}

extern parserDefinition *ValaParse(void) {
	g_type_init();
	static const char *const extensions [] = { "vala", "vapi", NULL };
	parserDefinition* def = parserNew ("Vala");
	def->kinds      = ValaKinds;
	def->kindCount  = KIND_COUNT (ValaKinds);
	def->extensions = extensions;
	def->parser     = findValaTags;
	return def;
}

static void findGenieTags (void) {
	if (visitor == NULL) {
		visitor = ctags_visitor_new();
	}
	ctags_visitor_parse_genie (visitor, getSourceFileName(), (CTagsEntryMaker) make_ctags_entry);
}

extern parserDefinition *GenieParser(void) {
	static const char *const extensions [] = { "gs", NULL };
	parserDefinition* def = parserNew ("Genie");
	def->kinds      = ValaKinds;
	def->kindCount  = KIND_COUNT (ValaKinds);
	def->extensions = extensions;
	def->parser     = findGenieTags;
	return def;
}
