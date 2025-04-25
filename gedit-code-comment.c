/**
Copyright (c) 2025 Florian Evaldsson

This software is provided 'as-is', without any express or implied
warranty. In no event will the authors be held liable for any damages
arising from the use of this software.

Permission is granted to anyone to use this software for any purpose,
including commercial applications, and to alter it and redistribute it
freely, subject to the following restrictions:

1. The origin of this software must not be misrepresented; you must not
   claim that you wrote the original software. If you use this software
   in a product, an acknowledgment in the product documentation would be
   appreciated but is not required.
2. Altered source versions must be plainly marked as such, and must not be
   misrepresented as being the original software.
3. This notice may not be removed or altered from any source distribution.
*/

#include "gedit-comment-plugin.h"
#include <string.h>
#include <glib/gi18n.h>
#include <gedit/gedit-debug.h>
#include <gedit/gedit-utils.h>
#include <gedit/gedit-app.h>
#include <gedit/gedit-window.h>
#include <gedit/gedit-app-activatable.h>
#include <gedit/gedit-window-activatable.h>

typedef int (*Comment_function)(GtkTextBuffer *buffer, GtkTextIter *start, GtkTextIter *end, const char *block_comment_start, 
                             const char *block_comment_end, const char *line_comment_start);
static void gedit_app_activatable_iface_init(GeditAppActivatableInterface *iface);
static void gedit_window_activatable_iface_init(GeditWindowActivatableInterface *iface);

struct _GeditCommentPluginPrivate
{
	GeditWindow *window;
	GSimpleAction *comment_action;
	GSimpleAction *uncomment_action;
	GeditApp *app;
	GeditMenuExtension *menu_ext;

	//GtkTextIter start, end; /* selection */
};

enum
{
	PROP_0,
	PROP_WINDOW,
	PROP_APP
};

G_DEFINE_DYNAMIC_TYPE_EXTENDED(GeditCommentPlugin, gedit_comment_plugin, PEAS_TYPE_EXTENSION_BASE, 0,
                               G_IMPLEMENT_INTERFACE_DYNAMIC(GEDIT_TYPE_APP_ACTIVATABLE, gedit_app_activatable_iface_init)
                               G_IMPLEMENT_INTERFACE_DYNAMIC(GEDIT_TYPE_WINDOW_ACTIVATABLE, gedit_window_activatable_iface_init)
                               G_ADD_PRIVATE_DYNAMIC(GeditCommentPlugin))

//////////////////////////////////

static void gtk_text_iter_go_to_line_end(GtkTextIter *itr)
{
	if(!gtk_text_iter_ends_line(itr))
	{
		//might move to the next row, need check
		gtk_text_iter_forward_to_line_end(itr);
	}
}

static gint get_line_length(GtkTextBuffer *buffer, GtkTextIter *iter)
{
	GtkTextIter line_start, line_end;
	
	// Get the start and end of the current line
	gtk_text_iter_assign(&line_start, iter);
	gtk_text_iter_set_line_offset(&line_start, 0);
	gtk_text_iter_assign(&line_end, &line_start);
	gtk_text_iter_go_to_line_end(&line_end);

	// Return the number of characters in the line
	return gtk_text_iter_get_offset(&line_end) - gtk_text_iter_get_offset(&line_start);
}

static int get_comment_definitions(GtkTextBuffer *buffer, const char **block_comment_start, 
                                   const char **block_comment_end, const char **line_comment_start)
{
	GtkSourceBuffer *sbuffer = GTK_SOURCE_BUFFER(buffer);
	GtkSourceLanguage *language = gtk_source_buffer_get_language(sbuffer);
	if (language)
	{
		(*block_comment_start) = gtk_source_language_get_metadata(language, "block-comment-start");
		(*block_comment_end) = gtk_source_language_get_metadata(language, "block-comment-end");
		(*line_comment_start) = gtk_source_language_get_metadata(language, "line-comment-start");
		return 0;
	}
	return -1;
}

static gboolean check_text_between_iters(GtkTextBuffer *buffer, GtkTextIter *start, GtkTextIter *end, 
                                         const size_t text_len, const char *text)
{
	const gint line_len=get_line_length(buffer,end);
	if(line_len>=text_len)
	{
		const gboolean found_line_comment = gtk_text_iter_forward_search(start, text, GTK_TEXT_SEARCH_TEXT_ONLY, NULL, NULL, end);
		
		return found_line_comment;
	}
	
	return FALSE;
}

static int remove_comment_if_exists(GtkTextBuffer *buffer, GtkTextIter *start, GtkTextIter *end, const size_t text_len, const char *text)
{
	gboolean found_line_comment = check_text_between_iters(buffer,start,end,text_len,text);
	
	if(found_line_comment)
	{
		gtk_text_buffer_delete(buffer, start, end);
		return 0;
	}
	
	return 1;
}

static void add_actual_comment_on_line(GtkTextBuffer *buffer, GtkTextIter *itr, const char *block_comment_start, 
                                       const char *block_comment_end, const char *line_comment_start)
{
	GtkTextIter this_itr;
	gtk_text_iter_assign(&this_itr, itr);
	
	gtk_text_iter_set_line_offset(&this_itr, 0);
	
	if(line_comment_start)
	{
		gtk_text_buffer_insert(buffer, &this_itr, line_comment_start, -1);
	}
	else
	{
		gtk_text_buffer_begin_user_action(buffer);
		
		gtk_text_buffer_insert(buffer, &this_itr, block_comment_start, -1);
		gtk_text_iter_go_to_line_end(&this_itr);
		gtk_text_buffer_insert(buffer, &this_itr, block_comment_end, -1);
		
		gtk_text_buffer_end_user_action(buffer);
	}
}

static void remove_actual_comment_on_line(GtkTextBuffer *buffer, GtkTextIter *itr, const char *block_comment_start, 
                                          const char *block_comment_end, const char *line_comment_start)
{
	GtkTextIter this_itr;
	GtkTextIter comment_end_iter;
	
	gtk_text_iter_assign(&this_itr, itr);
	gtk_text_iter_set_line_offset(&this_itr, 0);
	gtk_text_iter_assign(&comment_end_iter, &this_itr);
	
	const size_t block_comment_start_len=block_comment_start?strlen(block_comment_start):0;
	//
	gboolean is_multicomment=FALSE;
	
	if(line_comment_start && block_comment_start)
	{
		GtkTextIter test_end_iter;
		
		gtk_text_iter_assign(&test_end_iter, &this_itr);
		gtk_text_iter_forward_chars(&test_end_iter,block_comment_start_len);
		
		if(check_text_between_iters(buffer, &this_itr, &test_end_iter, block_comment_start_len, block_comment_start))
		{
			is_multicomment=TRUE;
		}
	}
	
	if(line_comment_start && !is_multicomment)
	{
		const size_t line_comment_start_len=strlen(line_comment_start);
		
		gtk_text_iter_forward_chars(&comment_end_iter,line_comment_start_len);
		
		remove_comment_if_exists(buffer, &this_itr, &comment_end_iter, line_comment_start_len, line_comment_start);
	}
	else
	{
		const size_t block_comment_end_len=strlen(block_comment_end);
	
		gtk_text_buffer_begin_user_action(buffer);
		
		gtk_text_iter_forward_chars(&comment_end_iter,block_comment_start_len);
		remove_comment_if_exists(buffer, &this_itr, &comment_end_iter, block_comment_start_len, block_comment_start);
		gtk_text_iter_go_to_line_end(&this_itr);
		gtk_text_iter_assign(&comment_end_iter, &this_itr);
		gtk_text_iter_backward_chars(&this_itr,strlen(block_comment_end));
		remove_comment_if_exists(buffer, &this_itr, &comment_end_iter, block_comment_end_len, block_comment_end);
		
		gtk_text_buffer_end_user_action(buffer);
	}
}

static int line_comment_code(GtkTextBuffer *buffer, GtkTextIter *start, GtkTextIter *end, const char *block_comment_start, 
                             const char *block_comment_end, const char *line_comment_start)
{
	if (end)
	{
		GtkTextIter start_iter;
		GtkTextIter end_iter;
	
		gtk_text_iter_assign(&start_iter, start);
		gtk_text_iter_assign(&end_iter, end);

		gtk_text_buffer_begin_user_action(buffer);

		int i = 0;
		do
		{
			// Move start_iter to the beginning of the current line
			add_actual_comment_on_line(buffer,&start_iter,block_comment_start,block_comment_end,line_comment_start);

			gtk_text_buffer_get_selection_bounds(buffer, &start_iter, &end_iter);

			// Move start_iter to the next line
			gtk_text_iter_forward_lines(&start_iter, i + 1);

			i++;
		}while(gtk_text_iter_get_offset(&end_iter) >= gtk_text_iter_get_offset(&start_iter));

		gtk_text_buffer_end_user_action(buffer);
	}
	else
	{
		add_actual_comment_on_line(buffer,start,block_comment_start,block_comment_end,line_comment_start);
	}
	
	return 0;
}

static void comment_code(GeditCommentPlugin *plugin, Comment_function com_funct)
{
	GeditCommentPluginPrivate *const priv = plugin->priv;
	GeditDocument *doc;
	GtkTextIter start, end;

	const char *block_comment_start = NULL;
	const char *block_comment_end = NULL;
	const char *line_comment_start = NULL;

	GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(gedit_window_get_active_view(priv->window)));
	
	get_comment_definitions(buffer,&block_comment_start,&block_comment_end,&line_comment_start);
	
	if(line_comment_start || (block_comment_start && block_comment_end))
	{
		doc = gedit_window_get_active_document(priv->window);
		g_return_if_fail(doc != NULL);

		if (gtk_text_buffer_get_selection_bounds(buffer, &start, &end))
		{
			/*gboolean found_other_block_comment =
				gtk_text_iter_forward_search(&start, block_comment_start, GTK_TEXT_SEARCH_TEXT_ONLY, NULL, NULL, &end);

			if (!found_other_block_comment)
			{
				if (block_comment_start && block_comment_end)
				{
					gtk_text_buffer_begin_user_action(buffer);

					gtk_text_buffer_insert(buffer, &start, block_comment_start, -1);
					if (gtk_text_buffer_get_selection_bounds(buffer, &start, &end))
					{
						gtk_text_buffer_insert(buffer, &end, block_comment_end, -1);
					}

					gtk_text_buffer_end_user_action(buffer);
				}
			}
			else*/
			{
				com_funct(buffer, &start, &end, block_comment_start,block_comment_end,line_comment_start);
			}
		}
		else
		{
			GtkTextIter iter;
			GtkTextMark *mark = gtk_text_buffer_get_insert(buffer);
			gtk_text_buffer_get_iter_at_mark(buffer, &iter, mark);

			com_funct(buffer, &iter, NULL, block_comment_start,block_comment_end,line_comment_start);
		}
	}
}

static int line_uncomment_code(GtkTextBuffer *buffer, GtkTextIter *start, GtkTextIter *end, const char *block_comment_start, 
                               const char *block_comment_end, const char *line_comment_start)
{
	GtkTextIter start_iter;
	GtkTextIter end_iter;

	if (end)
	{
		gtk_text_iter_assign(&start_iter, start);
		gtk_text_iter_assign(&end_iter, end);
		gtk_text_iter_set_line_offset(&end_iter, 0);

		gtk_text_buffer_begin_user_action(buffer);

		int i = 0;
		do
		{
			remove_actual_comment_on_line(buffer, &start_iter,block_comment_start,block_comment_end,line_comment_start);
			
			gtk_text_buffer_get_selection_bounds(buffer, &start_iter, &end_iter);

			// Move start_iter to the next line
			gtk_text_iter_forward_lines(&start_iter, i + 1);
			
			i++;
		}while(gtk_text_iter_get_offset(&end_iter) >= gtk_text_iter_get_offset(&start_iter));

		gtk_text_buffer_end_user_action(buffer);
	}
	else
	{
		remove_actual_comment_on_line(buffer, start,block_comment_start,block_comment_end,line_comment_start);
	}
	
	return 0;
}

static void comment_cb(GAction *action, GVariant *parameter, GeditCommentPlugin *plugin)
{
	comment_code(plugin,line_comment_code);
}

static void uncomment_cb(GAction *action, GVariant *parameter, GeditCommentPlugin *plugin)
{
	comment_code(plugin,line_uncomment_code);
}

static void update_ui(GeditCommentPlugin *plugin)
{
	GeditView *view;

	view = gedit_window_get_active_view(plugin->priv->window);

	g_simple_action_set_enabled(plugin->priv->comment_action, (view != NULL) && gtk_text_view_get_editable(GTK_TEXT_VIEW(view)));
	g_simple_action_set_enabled(plugin->priv->uncomment_action, (view != NULL) && gtk_text_view_get_editable(GTK_TEXT_VIEW(view)));
}

static void gedit_comment_plugin_app_activate(GeditAppActivatable *activatable)
{
	GeditCommentPluginPrivate *priv;
	GMenuItem *item;

	priv = GEDIT_COMMENT_PLUGIN(activatable)->priv;

	gtk_application_set_accels_for_action(GTK_APPLICATION(priv->app), "win.comment", (const gchar *[]){"<Primary>M", NULL});
	gtk_application_set_accels_for_action(GTK_APPLICATION(priv->app), "win.uncomment", (const gchar *[]){"<Primary><Shift>M", NULL});

	priv->menu_ext = gedit_app_activatable_extend_menu(activatable, "tools-section");

	item = g_menu_item_new(_("Comment"), "win.comment");
	gedit_menu_extension_append_menu_item(priv->menu_ext, item);
	g_object_unref(item);
	item = g_menu_item_new(_("Uncomment"), "win.uncomment");
	gedit_menu_extension_append_menu_item(priv->menu_ext, item);
	g_object_unref(item);
}

static void gedit_comment_plugin_app_deactivate(GeditAppActivatable *activatable)
{
	GeditCommentPluginPrivate *priv;

	priv = GEDIT_COMMENT_PLUGIN(activatable)->priv;

	g_clear_object(&priv->menu_ext);
}

static void gedit_comment_plugin_window_activate(GeditWindowActivatable *activatable)
{
	//GeditCommentPlugin *plugin = GEDIT_COMMENT_PLUGIN(activatable);

	GeditCommentPluginPrivate *priv;

	priv = GEDIT_COMMENT_PLUGIN(activatable)->priv;

	priv->comment_action = g_simple_action_new("comment", NULL);
	g_signal_connect(priv->comment_action, "activate", G_CALLBACK(comment_cb), activatable);
	g_action_map_add_action(G_ACTION_MAP(priv->window), G_ACTION(priv->comment_action));

	priv->uncomment_action = g_simple_action_new("uncomment", NULL);
	g_signal_connect(priv->uncomment_action, "activate", G_CALLBACK(uncomment_cb), activatable);
	g_action_map_add_action(G_ACTION_MAP(priv->window), G_ACTION(priv->uncomment_action));

	update_ui(GEDIT_COMMENT_PLUGIN(activatable));
}

static void gedit_comment_plugin_window_deactivate(GeditWindowActivatable *activatable)
{
	GeditCommentPluginPrivate *priv;

	priv = GEDIT_COMMENT_PLUGIN(activatable)->priv;
	g_action_map_remove_action(G_ACTION_MAP(priv->window), "comment");
	g_action_map_remove_action(G_ACTION_MAP(priv->window), "uncomment");
}

static void gedit_comment_plugin_window_update_state(GeditWindowActivatable *activatable)
{
	update_ui(GEDIT_COMMENT_PLUGIN(activatable));
}

static void gedit_comment_plugin_init(GeditCommentPlugin *plugin)
{
	plugin->priv = gedit_comment_plugin_get_instance_private(plugin);
}

static void gedit_comment_plugin_dispose(GObject *object)
{
	GeditCommentPlugin *plugin = GEDIT_COMMENT_PLUGIN(object);

	g_clear_object(&plugin->priv->comment_action);
	g_clear_object(&plugin->priv->uncomment_action);
	g_clear_object(&plugin->priv->window);
	g_clear_object(&plugin->priv->menu_ext);
	g_clear_object(&plugin->priv->app);

	G_OBJECT_CLASS(gedit_comment_plugin_parent_class)->dispose(object);
}

static void gedit_comment_plugin_finalize(GObject *object)
{
	G_OBJECT_CLASS(gedit_comment_plugin_parent_class)->finalize(object);
}

static void gedit_comment_plugin_set_property(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
	GeditCommentPlugin *plugin = GEDIT_COMMENT_PLUGIN(object);

	switch (prop_id)
	{
	case PROP_WINDOW:
		plugin->priv->window = GEDIT_WINDOW(g_value_dup_object(value));
		break;
	case PROP_APP:
		plugin->priv->app = GEDIT_APP(g_value_dup_object(value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
		break;
	}
}

static void gedit_comment_plugin_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	GeditCommentPlugin *plugin = GEDIT_COMMENT_PLUGIN(object);

	switch (prop_id)
	{
	case PROP_WINDOW:
		g_value_set_object(value, plugin->priv->window);
		break;
	case PROP_APP:
		g_value_set_object(value, plugin->priv->app);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
		break;
	}
}

static void gedit_comment_plugin_class_init(GeditCommentPluginClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);

	object_class->dispose = gedit_comment_plugin_dispose;
	object_class->finalize = gedit_comment_plugin_finalize;
	object_class->set_property = gedit_comment_plugin_set_property;
	object_class->get_property = gedit_comment_plugin_get_property;

	g_object_class_override_property(object_class, PROP_WINDOW, "window");
	g_object_class_override_property(object_class, PROP_APP, "app");
}

static void gedit_comment_plugin_class_finalize(GeditCommentPluginClass *klass)
{
}

static void gedit_app_activatable_iface_init(GeditAppActivatableInterface *iface)
{
	iface->activate = gedit_comment_plugin_app_activate;
	iface->deactivate = gedit_comment_plugin_app_deactivate;
}

static void gedit_window_activatable_iface_init(GeditWindowActivatableInterface *iface)
{
	iface->activate = gedit_comment_plugin_window_activate;
	iface->deactivate = gedit_comment_plugin_window_deactivate;
	iface->update_state = gedit_comment_plugin_window_update_state;
}

G_MODULE_EXPORT void peas_register_types(PeasObjectModule *module)
{
	gedit_comment_plugin_register_type(G_TYPE_MODULE(module));

	peas_object_module_register_extension_type(module, GEDIT_TYPE_APP_ACTIVATABLE, GEDIT_TYPE_COMMENT_PLUGIN);
	peas_object_module_register_extension_type(module, GEDIT_TYPE_WINDOW_ACTIVATABLE, GEDIT_TYPE_COMMENT_PLUGIN);
}
