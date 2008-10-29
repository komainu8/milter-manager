/* -*- Mode: C; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 *  Copyright (C) 2008  Kouhei Sutou <kou@cozmixng.org>
 *
 *  This library is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU Lesser General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include <gcutter.h>

#define shutdown inet_shutdown
#include <milter-manager-test-utils.h>
#include <milter/manager/milter-manager-children.h>
#undef shutdown

void test_foreach (void);
void test_negotiate (void);

static MilterManagerConfiguration *config;
static MilterManagerChildren *children;
static MilterOption *option;

static GError *actual_error;
static GError *expected_error;
static GList *actual_names;
static GList *expected_names;

static guint n_negotiate_reply_emitted;
static guint n_continue_emitted;
static guint n_reply_code_emitted;
static guint n_temporary_failure_emitted;
static guint n_reject_emitted;
static guint n_accept_emitted;
static guint n_discard_emitted;
static guint n_add_header_emitted;
static guint n_insert_header_emitted;
static guint n_change_header_emitted;
static guint n_change_from_emitted;
static guint n_add_receipt_emitted;
static guint n_delete_receipt_emitted;
static guint n_replace_body_emitted;
static guint n_progress_emitted;
static guint n_quarantine_emitted;
static guint n_connection_failure_emitted;
static guint n_shutdown_emitted;
static guint n_skip_emitted;
static guint n_error_emitted;
static guint n_finished_emitted;

static gchar *client_path;
static GList *clients;
static gboolean client_reaped;
static gboolean client_ready;

static void
cb_negotiate_reply (MilterManagerChildren *children,
                    MilterOption *option, MilterMacrosRequests *macros_requests,
                    gpointer user_data)
{
    n_negotiate_reply_emitted++;
}

static void
cb_continue (MilterManagerChildren *children, gpointer user_data)
{
    n_continue_emitted++;
}

static void
cb_reply_code (MilterManagerChildren *children, const gchar *code,
               gpointer user_data)
{
    n_reply_code_emitted++;
}

static void
cb_temporary_failure (MilterManagerChildren *children, gpointer user_data)
{
    n_temporary_failure_emitted++;
}

static void
cb_reject (MilterManagerChildren *children, gpointer user_data)
{
    n_reject_emitted++;
}

static void
cb_accept (MilterManagerChildren *children, gpointer user_data)
{
    n_accept_emitted++;
}

static void
cb_discard (MilterManagerChildren *children, gpointer user_data)
{
    n_discard_emitted++;
}

static void
cb_add_header (MilterManagerChildren *children,
               const gchar *name, const gchar *value,
               gpointer user_data)
{
    n_add_header_emitted++;
}

static void
cb_insert_header (MilterManagerChildren *children,
                  guint32 index, const gchar *name, const gchar *value,
                  gpointer user_data)
{
    n_insert_header_emitted++;
}

static void
cb_change_header (MilterManagerChildren *children,
                  const gchar *name, guint32 index, const gchar *value,
                  gpointer user_data)
{
    n_change_header_emitted++;
}

static void
cb_change_from (MilterManagerChildren *children,
                const gchar *from, const gchar *parameters,
                gpointer user_data)
{
    n_change_from_emitted++;
}

static void
cb_add_receipt (MilterManagerChildren *children,
                const gchar *receipt, const gchar *parameters,
                gpointer user_data)
{
    n_add_receipt_emitted++;
}

static void
cb_delete_receipt (MilterManagerChildren *children, const gchar *receipt,
                   gpointer user_data)
{
    n_delete_receipt_emitted++;
}

static void
cb_replace_body (MilterManagerChildren *children,
                 const gchar *body, gsize body_size,
                 gpointer user_data)
{
    n_replace_body_emitted++;
}

static void
cb_progress (MilterManagerChildren *children, gpointer user_data)
{
    n_progress_emitted++;
}

static void
cb_quarantine (MilterManagerChildren *children, const gchar *reason,
               gpointer user_data)
{
    n_quarantine_emitted++;
}

static void
cb_connection_failure (MilterManagerChildren *children, gpointer user_data)
{
    n_connection_failure_emitted++;
}

static void
cb_shutdown (MilterManagerChildren *children, gpointer user_data)
{
    n_shutdown_emitted++;
}

static void
cb_skip (MilterManagerChildren *children, gpointer user_data)
{
    n_skip_emitted++;
}

static void
cb_error (MilterManagerChildren *children, GError *error, gpointer user_data)
{
    n_error_emitted++;
}

static void
cb_finished (MilterManagerChildren *children, gpointer user_data)
{
    n_finished_emitted++;
}


static void
setup_signals (MilterManagerChildren *children)
{
#define CONNECT(name)                                                   \
    g_signal_connect(children, #name, G_CALLBACK(cb_ ## name), NULL)

    CONNECT(negotiate_reply);
    CONNECT(continue);
    CONNECT(reply_code);
    CONNECT(temporary_failure);
    CONNECT(reject);
    CONNECT(accept);
    CONNECT(discard);
    CONNECT(add_header);
    CONNECT(insert_header);
    CONNECT(change_header);
    CONNECT(change_from);
    CONNECT(add_receipt);
    CONNECT(delete_receipt);
    CONNECT(replace_body);
    CONNECT(progress);
    CONNECT(quarantine);
    CONNECT(connection_failure);
    CONNECT(shutdown);
    CONNECT(skip);

    CONNECT(error);
    /* CONNECT(finished); */

#undef CONNECT
}

static void
cb_output_received (GCutEgg *egg, const gchar *chunk, gsize size,
                    gpointer user_data)
{
    if (g_str_has_prefix(chunk, "ready")) {
        client_ready = TRUE;
    } else {
        GString *string;

        string = g_string_new_len(chunk, size);
        g_print("[OUTPUT] <%s>\n", string->str);
        g_string_free(string, TRUE);
    }
}

static void
cb_error_received (GCutEgg *egg, const gchar *chunk, gsize size,
                   gpointer user_data)
{
    GString *string;

    string = g_string_new_len(chunk, size);
    g_print("[ERROR] <%s>\n", string->str);
    g_string_free(string, TRUE);
}

static void
cb_reaped (GCutEgg *egg, gint status, gpointer user_data)
{
    client_reaped = TRUE;
}

static void
setup_egg_signals (GCutEgg *egg)
{
#define CONNECT(name)                                                   \
    g_signal_connect(egg, #name, G_CALLBACK(cb_ ## name), NULL)

    CONNECT(output_received);
    CONNECT(error_received);
    CONNECT(reaped);

#undef CONNECT
}

static void
teardown_egg_signals (GCutEgg *egg)
{
#define DISCONNECT(name)                                                \
    g_signal_handlers_disconnect_by_func(egg,                           \
                                         G_CALLBACK(cb_ ## name),       \
                                         NULL)

    DISCONNECT(output_received);
    DISCONNECT(error_received);
    DISCONNECT(reaped);

#undef DISCONNECT
}

static void
free_egg (GCutEgg *egg)
{
    teardown_egg_signals(egg);
    g_object_unref(egg);
}

static void
make_egg (const gchar *command, ...)
{
    GCutEgg *egg;
    va_list args;

    va_start(args, command);
    egg = gcut_egg_new_va_list(command, args);
    va_end(args);

    setup_egg_signals(egg);

    clients = g_list_prepend(clients, egg);
}

static void
hatch_egg (GCutEgg *egg)
{
    GError *error = NULL;
    client_ready = FALSE;
    client_reaped = FALSE;

    gcut_egg_hatch(egg, &error);
    gcut_assert_error(error);

    while (!client_ready && !client_reaped)
        g_main_context_iteration(NULL, TRUE);
    cut_assert_false(client_reaped);
}

void
setup (void)
{
    config = milter_manager_configuration_new(NULL);
    children = milter_manager_children_new(config);
    setup_signals(children);

    option = NULL;

    actual_error = NULL;
    expected_error = NULL;

    actual_names = NULL;
    expected_names = NULL;

    n_negotiate_reply_emitted = 0;
    n_continue_emitted = 0;
    n_reply_code_emitted = 0;
    n_temporary_failure_emitted = 0;
    n_reject_emitted = 0;
    n_accept_emitted = 0;
    n_discard_emitted = 0;
    n_add_header_emitted = 0;
    n_insert_header_emitted = 0;
    n_change_header_emitted = 0;
    n_change_from_emitted = 0;
    n_add_receipt_emitted = 0;
    n_delete_receipt_emitted = 0;
    n_replace_body_emitted = 0;
    n_progress_emitted = 0;
    n_quarantine_emitted = 0;
    n_connection_failure_emitted = 0;
    n_shutdown_emitted = 0;
    n_skip_emitted = 0;
    n_error_emitted = 0;
    n_finished_emitted = 0;

    client_path = g_build_filename(milter_manager_test_get_base_dir(),
                                   "lib",
                                   "milter-test-client.rb",
                                   NULL);
    clients = NULL;
}

void
teardown (void)
{
    if (config)
        g_object_unref(config);
    if (children)
        g_object_unref(children);

    if (option)
        g_object_unref(option);

    if (actual_error)
        g_error_free(actual_error);
    if (expected_error)
        g_error_free(expected_error);

    if (actual_names)
        gcut_list_string_free(actual_names);
    if (expected_names)
        gcut_list_string_free(expected_names);

    if (client_path)
        g_free(client_path);
    if (clients) {
        g_list_foreach(clients, (GFunc)free_egg, NULL);
        g_list_free(clients);
    }
}

static void
each_child (MilterManagerChild *child, gpointer user_data)
{
    gchar *name = NULL;
    g_object_get(child,
                 "name", &name,
                 NULL);

    actual_names = g_list_append(actual_names, name);
}

void
test_foreach (void)
{
    expected_names = gcut_list_string_new("1", "2", "3", NULL);

    milter_manager_children_add_child(children,
                                      milter_manager_child_new("1"));
    milter_manager_children_add_child(children,
                                      milter_manager_child_new("2"));
    milter_manager_children_add_child(children,
                                      milter_manager_child_new("3"));
    milter_manager_children_foreach(children,
                                    (GFunc)each_child, NULL);

    gcut_assert_equal_list_string(expected_names, actual_names);
}

static gboolean
cb_timeout_waiting (gpointer data)
{
    gboolean *waiting = data;

    *waiting = FALSE;
    return FALSE;
}

#define wait_reply(actual)                      \
    cut_trace_with_info_expression(             \
        wait_reply_helper(1, &actual),          \
        wait_reply(actual))

static void
wait_reply_helper (guint expected, guint *actual)
{
    gboolean timeout_waiting = TRUE;
    guint timeout_waiting_id;

    timeout_waiting_id = g_timeout_add_seconds(1, cb_timeout_waiting,
                                               &timeout_waiting);
    while (timeout_waiting && expected > *actual) {
        g_main_context_iteration(NULL, TRUE);
    }
    g_source_remove(timeout_waiting_id);

    cut_assert_true(timeout_waiting, "timeout");
    cut_assert_equal_uint(expected, *actual);
}

void
test_negotiate (void)
{
    MilterManagerEgg *egg;
    MilterManagerChild *child;
    GError *error = NULL;

    option = milter_option_new(2,
                               MILTER_ACTION_ADD_HEADERS |
                               MILTER_ACTION_CHANGE_BODY,
                               MILTER_STEP_NONE);

    make_egg(client_path, "--print-status",
             "--timeout", "1.0", "--port", "10026", NULL);
    make_egg(client_path, "--print-status",
             "--timeout", "1.0", "--port", "10027", NULL);
    g_list_foreach(clients, (GFunc)hatch_egg, NULL);

    egg = milter_manager_egg_new("milter@10026");
    milter_manager_egg_set_connection_spec(egg, "inet:10026@localhost", &error);
    child = milter_manager_egg_hatch(egg);
    milter_manager_children_add_child(children, child);
    g_object_unref(egg);
    g_object_unref(child);

    egg = milter_manager_egg_new("milter@10027");
    milter_manager_egg_set_connection_spec(egg, "inet:10027@localhost", &error);
    child = milter_manager_egg_hatch(egg);
    milter_manager_children_add_child(children, child);
    g_object_unref(egg);
    g_object_unref(child);

    milter_manager_children_negotiate(children, option);
    wait_reply(n_negotiate_reply_emitted);
    cut_assert_equal_uint(1, n_negotiate_reply_emitted);
}


/*
vi:ts=4:nowrap:ai:expandtab:sw=4
*/
