/*
 * Hotspot 2.0 client - Web browser using WebKit
 * Copyright (c) 2013, Qualcomm Atheros, Inc.
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#include "includes.h"
#ifdef USE_WEBKIT2
#include <webkitgtk-4.0/webkit2/webkit2.h>
#else
#include <webkit/webkit.h>
#endif

#include "common.h"
#include "browser.h"


struct browser_context {
	GtkWidget *win;
	int success;
	int progress;
	char *hover_link;
	char *title;
};

static void win_cb_destroy(GtkWidget *win, struct browser_context *ctx)
{
	wpa_printf(MSG_DEBUG, "BROWSER:%s", __func__);
	gtk_main_quit();
}


static void browser_update_title(struct browser_context *ctx)
{
	char buf[100];

	if (ctx->hover_link) {
		gtk_window_set_title(GTK_WINDOW(ctx->win), ctx->hover_link);
		return;
	}

	if (ctx->progress == 100) {
		gtk_window_set_title(GTK_WINDOW(ctx->win),
				     ctx->title ? ctx->title :
				     "Hotspot 2.0 client");
		return;
	}

	snprintf(buf, sizeof(buf), "[%d%%] %s", ctx->progress,
		 ctx->title ? ctx->title : "Hotspot 2.0 client");
	gtk_window_set_title(GTK_WINDOW(ctx->win), buf);
}


static void view_cb_notify_progress(WebKitWebView *view, GParamSpec *pspec,
				    struct browser_context *ctx)
{
#ifdef USE_WEBKIT2
	ctx->progress = 100 * webkit_web_view_get_estimated_load_progress(view);
#else
	ctx->progress = 100 * webkit_web_view_get_progress(view);
#endif
	wpa_printf(MSG_DEBUG, "BROWSER:%s progress=%d", __func__,
		   ctx->progress);
	browser_update_title(ctx);
}


static void view_cb_notify_load_status(WebKitWebView *view, GParamSpec *pspec,
				       struct browser_context *ctx)
{
#ifdef USE_WEBKIT2
	int status = webkit_web_view_get_estimated_load_progress(view);
#else
	int status = webkit_web_view_get_load_status(view);
#endif
	wpa_printf(MSG_DEBUG, "BROWSER:%s load-status=%d uri=%s",
		   __func__, status, webkit_web_view_get_uri(view));
}


static void view_cb_resource_request_starting(WebKitWebView *view,
#ifndef USE_WEBKIT2
					      WebKitWebFrame *frame,
#endif
					      WebKitWebResource *res,
#ifdef USE_WEBKIT2
					      WebKitURIRequest *req,
#else
					      WebKitNetworkRequest *req,
					      WebKitNetworkResponse *resp,
#endif
					      struct browser_context *ctx)
{
#ifdef USE_WEBKIT2
	const gchar *uri = webkit_uri_request_get_uri(req);
#else
	const gchar *uri = webkit_network_request_get_uri(req);
#endif
	wpa_printf(MSG_DEBUG, "BROWSER:%s uri=%s", __func__, uri);
	if (g_str_has_suffix(uri, "/favicon.ico")) {
#ifdef USE_WEBKIT2
		webkit_uri_request_set_uri(req, "about:blank");
#else
		webkit_network_request_set_uri(req, "about:blank");
#endif
	}

	if (g_str_has_prefix(uri, "osu://")) {
		ctx->success = atoi(uri + 6);
		gtk_main_quit();
	}
	if (g_str_has_prefix(uri, "http://localhost:12345")) {
		/*
		 * This is used as a special trigger to indicate that the
		 * user exchange has been completed.
		 */
		ctx->success = 1;
		gtk_main_quit();
	}
}


static gboolean view_cb_mime_type_policy_decision(
	WebKitWebView *view,
#ifndef USE_WEBKIT2
	WebKitWebFrame *frame, WebKitNetworkRequest *req,
	gchar *mime, WebKitWebPolicyDecision *policy,
#else
	WebKitPolicyDecision *policy,
	WebKitPolicyDecisionType type,
#endif
	struct browser_context *ctx)
{
#ifdef USE_WEBKIT2
	switch (type) {
	case WEBKIT_POLICY_DECISION_TYPE_RESPONSE: {
		/* This function makes webkit send a download signal for all unknown
		   mime types. */
		WebKitResponsePolicyDecision *response = WEBKIT_RESPONSE_POLICY_DECISION(policy);
		if (!webkit_response_policy_decision_is_mime_type_supported (response)) {
			webkit_policy_decision_download (policy);
			return TRUE;
		}
		break;
	}
	default:
		break;
	}
#else
	wpa_printf(MSG_DEBUG, "BROWSER:%s mime=%s", __func__, mime);

	if (!webkit_web_view_can_show_mime_type(view, mime)) {
		webkit_web_policy_decision_download(policy);
		return TRUE;
	}
#endif

	return FALSE;
}

#ifndef USE_WEBKIT2
static gboolean view_cb_download_requested(WebKitWebView *view,
					   WebKitDownload *dl,
					   struct browser_context *ctx)
{
	const gchar *uri;
	uri = webkit_download_get_uri(dl);
	wpa_printf(MSG_DEBUG, "BROWSER:%s uri=%s", __func__, uri);
	return FALSE;
}
#endif


static void view_cb_hovering_over_link(WebKitWebView *view, gchar *title,
				       gchar *uri, struct browser_context *ctx)
{
	wpa_printf(MSG_DEBUG, "BROWSER:%s title=%s uri=%s", __func__, title,
		   uri);
	os_free(ctx->hover_link);
	if (uri)
		ctx->hover_link = os_strdup(uri);
	else
		ctx->hover_link = NULL;

	browser_update_title(ctx);
}

#ifndef USE_WEBKIT2
static void view_cb_title_changed(WebKitWebView *view, WebKitWebFrame *frame,
				  const char *title,
				  struct browser_context *ctx)
{
	wpa_printf(MSG_DEBUG, "BROWSER:%s title=%s", __func__, title);
	os_free(ctx->title);
	ctx->title = os_strdup(title);
	browser_update_title(ctx);
}
#endif

int hs20_web_browser(const char *url)
{
	GtkWidget *scroll;
	WebKitWebView *view;
#ifdef USE_WEBKIT2
	WebKitSettings *settings;
#else
	WebKitWebSettings *settings;
	SoupSession *s;
#endif
	struct browser_context ctx;

	memset(&ctx, 0, sizeof(ctx));
	if (!gtk_init_check(NULL, NULL))
		return -1;

#ifndef USE_WEBKIT2
	/* TODO-BEN:  Not sure how to do this in webkit2 */
	s = webkit_get_default_session();
	g_object_set(G_OBJECT(s), "ssl-ca-file",
		     "/etc/ssl/certs/ca-certificates.crt", NULL);
	g_object_set(G_OBJECT(s), "ssl-strict", FALSE, NULL);
#endif

	ctx.win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	/* This is deprecated, evidently...not sure what to replace it with, if anything */
	gtk_window_set_wmclass(GTK_WINDOW(ctx.win), "Hotspot 2.0 client",
			       "Hotspot 2.0 client");
	gtk_window_set_default_size(GTK_WINDOW(ctx.win), 800, 600);

	scroll = gtk_scrolled_window_new(NULL, NULL);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
				       GTK_POLICY_NEVER, GTK_POLICY_NEVER);

	g_signal_connect(G_OBJECT(ctx.win), "destroy",
			 G_CALLBACK(win_cb_destroy), &ctx);

	view = WEBKIT_WEB_VIEW(webkit_web_view_new());
	g_signal_connect(G_OBJECT(view), "notify::progress",
			 G_CALLBACK(view_cb_notify_progress), &ctx);
	g_signal_connect(G_OBJECT(view), "notify::load-status",
			 G_CALLBACK(view_cb_notify_load_status), &ctx);
#ifdef USE_WEBKIT2
	g_signal_connect(G_OBJECT(view), "resource-load-started",
			 G_CALLBACK(view_cb_resource_request_starting), &ctx);
	g_signal_connect(G_OBJECT(view), "decide-policy",
			 G_CALLBACK(view_cb_mime_type_policy_decision), &ctx);
	/* TODO-BEN: Implement these?
	  g_signal_connect(G_OBJECT(view), "download-started",
			 G_CALLBACK(view_cb_download_requested), &ctx);
	  g_signal_connect(G_OBJECT(view), "notify::title",
			 G_CALLBACK(view_cb_title_changed), &ctx);
	*/
#else
	g_signal_connect(G_OBJECT(view), "resource-request-starting",
			 G_CALLBACK(view_cb_resource_request_starting), &ctx);
	g_signal_connect(G_OBJECT(view), "mime-type-policy-decision-requested",
			 G_CALLBACK(view_cb_mime_type_policy_decision), &ctx);
	g_signal_connect(G_OBJECT(view), "download-requested",
			 G_CALLBACK(view_cb_download_requested), &ctx);
	g_signal_connect(G_OBJECT(view), "title-changed",
			 G_CALLBACK(view_cb_title_changed), &ctx);
#endif

	g_signal_connect(G_OBJECT(view), "hovering-over-link",
			 G_CALLBACK(view_cb_hovering_over_link), &ctx);

	gtk_container_add(GTK_CONTAINER(scroll), GTK_WIDGET(view));
	gtk_container_add(GTK_CONTAINER(ctx.win), GTK_WIDGET(scroll));

	gtk_widget_grab_focus(GTK_WIDGET(view));
	gtk_widget_show_all(ctx.win);

	settings = webkit_web_view_get_settings(view);
	g_object_set(G_OBJECT(settings), "user-agent",
		     "Mozilla/5.0 (X11; U; Unix; en-US) "
		     "AppleWebKit/537.15 (KHTML, like Gecko) "
		     "hs20-client/1.0", NULL);
	g_object_set(G_OBJECT(settings), "auto-load-images", TRUE, NULL);

	webkit_web_view_load_uri(view, url);

	gtk_main();
	gtk_widget_destroy(ctx.win);
	while (gtk_events_pending())
		gtk_main_iteration();

	free(ctx.hover_link);
	free(ctx.title);
	return ctx.success;
}
