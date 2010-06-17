/*
 *
 *  bluez-tools - a set of tools to manage bluetooth devices for linux
 *
 *  Copyright (C) 2010  Alexander Orlenko <zxteam@gmail.com>
 *
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "dbus-common.h"
#include "marshallers.h"
#include "device.h"

#define BLUEZ_DBUS_DEVICE_INTERFACE "org.bluez.Device"

#define DEVICE_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), DEVICE_TYPE, DevicePrivate))

struct _DevicePrivate {
	DBusGProxy *dbus_g_proxy;
};

G_DEFINE_TYPE(Device, device, G_TYPE_OBJECT);

enum {
	PROP_0,

	PROP_DBUS_OBJECT_PATH, /* readwrite, construct only */
	PROP_ADAPTER, /* readonly */
	PROP_ADDRESS, /* readonly */
	PROP_ALIAS, /* readwrite */
	PROP_BLOCKED, /* readwrite */
	PROP_CLASS, /* readonly */
	PROP_CONNECTED, /* readonly */
	PROP_ICON, /* readonly */
	PROP_LEGACY_PAIRING, /* readonly */
	PROP_NAME, /* readonly */
	PROP_NODES, /* readonly */
	PROP_PAIRED, /* readonly */
	PROP_TRUSTED, /* readwrite */
	PROP_UUIDS /* readonly */
};

enum {
	DISCONNECT_REQUESTED,
	NODE_CREATED,
	NODE_REMOVED,
	PROPERTY_CHANGED,

	LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = {0};

static void _device_get_property(GObject *object, guint property_id, GValue *value, GParamSpec *pspec);
static void _device_set_property(GObject *object, guint property_id, const GValue *value, GParamSpec *pspec);

static void disconnect_requested_handler(DBusGProxy *dbus_g_proxy, gpointer data);
static void node_created_handler(DBusGProxy *dbus_g_proxy, const gchar *node, gpointer data);
static void node_removed_handler(DBusGProxy *dbus_g_proxy, const gchar *node, gpointer data);
static void property_changed_handler(DBusGProxy *dbus_g_proxy, const gchar *name, const GValue *value, gpointer data);

static void device_class_init(DeviceClass *klass)
{
	g_type_class_add_private(klass, sizeof(DevicePrivate));

	/* Properties registration */
	GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
	GParamSpec *pspec;

	gobject_class->get_property = _device_get_property;
	gobject_class->set_property = _device_set_property;

	/* object DBusObjectPath [readwrite, construct only] */
	pspec = g_param_spec_string("DBusObjectPath", "dbus_object_path", "Adapter D-Bus object path", NULL, G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY);
	g_object_class_install_property(gobject_class, PROP_DBUS_OBJECT_PATH, pspec);

	/* object Adapter [readonly] */
	pspec = g_param_spec_string("Adapter", NULL, NULL, NULL, G_PARAM_READABLE);
	g_object_class_install_property(gobject_class, PROP_ADAPTER, pspec);

	/* string Address [readonly] */
	pspec = g_param_spec_string("Address", NULL, NULL, NULL, G_PARAM_READABLE);
	g_object_class_install_property(gobject_class, PROP_ADDRESS, pspec);

	/* string Alias [readwrite] */
	pspec = g_param_spec_string("Alias", NULL, NULL, NULL, G_PARAM_READWRITE);
	g_object_class_install_property(gobject_class, PROP_ALIAS, pspec);

	/* boolean Blocked [readwrite] */
	pspec = g_param_spec_boolean("Blocked", NULL, NULL, FALSE, G_PARAM_READWRITE);
	g_object_class_install_property(gobject_class, PROP_BLOCKED, pspec);

	/* uint32 Class [readonly] */
	pspec = g_param_spec_uint("Class", NULL, NULL, 0, 65535, 0, G_PARAM_READABLE);
	g_object_class_install_property(gobject_class, PROP_CLASS, pspec);

	/* boolean Connected [readonly] */
	pspec = g_param_spec_boolean("Connected", NULL, NULL, FALSE, G_PARAM_READABLE);
	g_object_class_install_property(gobject_class, PROP_CONNECTED, pspec);

	/* string Icon [readonly] */
	pspec = g_param_spec_string("Icon", NULL, NULL, NULL, G_PARAM_READABLE);
	g_object_class_install_property(gobject_class, PROP_ICON, pspec);

	/* boolean LegacyPairing [readonly] */
	pspec = g_param_spec_boolean("LegacyPairing", NULL, NULL, FALSE, G_PARAM_READABLE);
	g_object_class_install_property(gobject_class, PROP_LEGACY_PAIRING, pspec);

	/* string Name [readonly] */
	pspec = g_param_spec_string("Name", NULL, NULL, NULL, G_PARAM_READABLE);
	g_object_class_install_property(gobject_class, PROP_NAME, pspec);

	/* array{object} Nodes [readonly] */
	pspec = g_param_spec_boxed("Nodes", NULL, NULL, G_TYPE_PTR_ARRAY, G_PARAM_READABLE);
	g_object_class_install_property(gobject_class, PROP_NODES, pspec);

	/* boolean Paired [readonly] */
	pspec = g_param_spec_boolean("Paired", NULL, NULL, FALSE, G_PARAM_READABLE);
	g_object_class_install_property(gobject_class, PROP_PAIRED, pspec);

	/* boolean Trusted [readwrite] */
	pspec = g_param_spec_boolean("Trusted", NULL, NULL, FALSE, G_PARAM_READWRITE);
	g_object_class_install_property(gobject_class, PROP_TRUSTED, pspec);

	/* array{string} UUIDs [readonly] */
	pspec = g_param_spec_boxed("UUIDs", NULL, NULL, G_TYPE_PTR_ARRAY, G_PARAM_READABLE);
	g_object_class_install_property(gobject_class, PROP_UUIDS, pspec);

	/* Signals registation */
	signals[DISCONNECT_REQUESTED] = g_signal_new("DisconnectRequested",
			G_TYPE_FROM_CLASS(gobject_class),
			G_SIGNAL_RUN_LAST | G_SIGNAL_NO_RECURSE | G_SIGNAL_NO_HOOKS,
			0, NULL, NULL,
			g_cclosure_marshal_VOID__VOID,
			G_TYPE_NONE, 0);

	signals[NODE_CREATED] = g_signal_new("NodeCreated",
			G_TYPE_FROM_CLASS(gobject_class),
			G_SIGNAL_RUN_LAST | G_SIGNAL_NO_RECURSE | G_SIGNAL_NO_HOOKS,
			0, NULL, NULL,
			g_cclosure_marshal_VOID__STRING,
			G_TYPE_NONE, 1, G_TYPE_STRING);

	signals[NODE_REMOVED] = g_signal_new("NodeRemoved",
			G_TYPE_FROM_CLASS(gobject_class),
			G_SIGNAL_RUN_LAST | G_SIGNAL_NO_RECURSE | G_SIGNAL_NO_HOOKS,
			0, NULL, NULL,
			g_cclosure_marshal_VOID__STRING,
			G_TYPE_NONE, 1, G_TYPE_STRING);

	signals[PROPERTY_CHANGED] = g_signal_new("PropertyChanged",
			G_TYPE_FROM_CLASS(gobject_class),
			G_SIGNAL_RUN_LAST | G_SIGNAL_NO_RECURSE | G_SIGNAL_NO_HOOKS,
			0, NULL, NULL,
			g_cclosure_bluez_marshal_VOID__STRING_BOXED,
			G_TYPE_NONE, 2, G_TYPE_STRING, G_TYPE_VALUE);
}

static void device_init(Device *self)
{
	self->priv = DEVICE_GET_PRIVATE(self);

	g_assert(conn != NULL);
}

static void device_post_init(Device *self)
{
	g_assert(self->priv->dbus_g_proxy != NULL);

	/* DBUS signals connection */

	/* DisconnectRequested() */
	dbus_g_proxy_add_signal(self->priv->dbus_g_proxy, "DisconnectRequested", G_TYPE_INVALID);
	dbus_g_proxy_connect_signal(self->priv->dbus_g_proxy, "DisconnectRequested", G_CALLBACK(disconnect_requested_handler), self, NULL);

	/* NodeCreated(object node) */
	dbus_g_proxy_add_signal(self->priv->dbus_g_proxy, "NodeCreated", DBUS_TYPE_G_OBJECT_PATH, G_TYPE_INVALID);
	dbus_g_proxy_connect_signal(self->priv->dbus_g_proxy, "NodeCreated", G_CALLBACK(node_created_handler), self, NULL);

	/* NodeRemoved(object node) */
	dbus_g_proxy_add_signal(self->priv->dbus_g_proxy, "NodeRemoved", DBUS_TYPE_G_OBJECT_PATH, G_TYPE_INVALID);
	dbus_g_proxy_connect_signal(self->priv->dbus_g_proxy, "NodeRemoved", G_CALLBACK(node_removed_handler), self, NULL);

	/* PropertyChanged(string name, variant value) */
	dbus_g_proxy_add_signal(self->priv->dbus_g_proxy, "PropertyChanged", G_TYPE_STRING, G_TYPE_VALUE, G_TYPE_INVALID);
	dbus_g_proxy_connect_signal(self->priv->dbus_g_proxy, "PropertyChanged", G_CALLBACK(property_changed_handler), self, NULL);
}

static void _device_get_property(GObject *object, guint property_id, GValue *value, GParamSpec *pspec)
{
	Device *self = DEVICE(object);

	GHashTable *properties = device_get_properties(self, NULL);
	if (properties == NULL) {
		return;
	}

	switch (property_id) {
	case PROP_DBUS_OBJECT_PATH:
		g_value_set_string(value, g_strdup(dbus_g_proxy_get_path(self->priv->dbus_g_proxy)));
		break;

	case PROP_ADAPTER:
		g_value_set_string(value, g_value_dup_string(g_hash_table_lookup(properties, "Adapter")));
		break;

	case PROP_ADDRESS:
		g_value_set_string(value, g_value_dup_string(g_hash_table_lookup(properties, "Address")));
		break;

	case PROP_ALIAS:
		g_value_set_string(value, g_value_dup_string(g_hash_table_lookup(properties, "Alias")));
		break;

	case PROP_BLOCKED:
		g_value_set_boolean(value, g_value_get_boolean(g_hash_table_lookup(properties, "Blocked")));
		break;

	case PROP_CLASS:
		g_value_set_uint(value, g_value_get_uint(g_hash_table_lookup(properties, "Class")));
		break;

	case PROP_CONNECTED:
		g_value_set_boolean(value, g_value_get_boolean(g_hash_table_lookup(properties, "Connected")));
		break;

	case PROP_ICON:
		g_value_set_string(value, g_value_dup_string(g_hash_table_lookup(properties, "Icon")));
		break;

	case PROP_LEGACY_PAIRING:
		g_value_set_boolean(value, g_value_get_boolean(g_hash_table_lookup(properties, "LegacyPairing")));
		break;

	case PROP_NAME:
		g_value_set_string(value, g_value_dup_string(g_hash_table_lookup(properties, "Name")));
		break;

	case PROP_NODES:
		g_value_set_boxed(value, g_value_dup_boxed(g_hash_table_lookup(properties, "Nodes")));
		break;

	case PROP_PAIRED:
		g_value_set_boolean(value, g_value_get_boolean(g_hash_table_lookup(properties, "Paired")));
		break;

	case PROP_TRUSTED:
		g_value_set_boolean(value, g_value_get_boolean(g_hash_table_lookup(properties, "Trusted")));
		break;

	case PROP_UUIDS:
		g_value_set_boxed(value, g_value_dup_boxed(g_hash_table_lookup(properties, "UUIDs")));
		break;

	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
		break;
	}

	g_hash_table_unref(properties);
}

static void _device_set_property(GObject *object, guint property_id, const GValue *value, GParamSpec *pspec)
{
	Device *self = DEVICE(object);

	switch (property_id) {
	case PROP_DBUS_OBJECT_PATH:
	{
		const gchar *dbus_object_path = g_value_get_string(value);
		g_assert(dbus_object_path != NULL);
		g_assert(self->priv->dbus_g_proxy == NULL);
		self->priv->dbus_g_proxy = dbus_g_proxy_new_for_name(conn, BLUEZ_DBUS_NAME, dbus_object_path, BLUEZ_DBUS_DEVICE_INTERFACE);
		device_post_init(self);
	}
		break;

	case PROP_ALIAS:
	{
		GError *error = NULL;
		device_set_property(self, "Alias", value, &error);
		if (error != NULL) {
			g_print("%s: %s\n", g_get_prgname(), error->message);
			g_error_free(error);
		}
	}
		break;

	case PROP_BLOCKED:
	{
		GError *error = NULL;
		device_set_property(self, "Blocked", value, &error);
		if (error != NULL) {
			g_print("%s: %s\n", g_get_prgname(), error->message);
			g_error_free(error);
		}
	}
		break;

	case PROP_TRUSTED:
	{
		GError *error = NULL;
		device_set_property(self, "Trusted", value, &error);
		if (error != NULL) {
			g_print("%s: %s\n", g_get_prgname(), error->message);
			g_error_free(error);
		}
	}
		break;

	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
		break;
	}
}

/* Methods */

/* void CancelDiscovery() */
void device_cancel_discovery(Device *self, GError **error)
{
	g_assert(self != NULL);

	dbus_g_proxy_call(self->priv->dbus_g_proxy, "CancelDiscovery", error, G_TYPE_INVALID, G_TYPE_INVALID);
}

/* object CreateNode(string uuid) */
gchar *device_create_node(Device *self, const gchar *uuid, GError **error)
{
	g_assert(self != NULL);

	gchar *ret;

	if (!dbus_g_proxy_call(self->priv->dbus_g_proxy, "CreateNode", error, G_TYPE_STRING, uuid, G_TYPE_INVALID, DBUS_TYPE_G_OBJECT_PATH, &ret, G_TYPE_INVALID)) {
		return NULL;
	}

	return ret;
}

/* void Disconnect() */
void device_disconnect(Device *self, GError **error)
{
	g_assert(self != NULL);

	dbus_g_proxy_call(self->priv->dbus_g_proxy, "Disconnect", error, G_TYPE_INVALID, G_TYPE_INVALID);
}

/* dict DiscoverServices(string pattern) */
GHashTable *device_discover_services(Device *self, const gchar *pattern, GError **error)
{
	g_assert(self != NULL);

	GHashTable *ret;

	if (!dbus_g_proxy_call(self->priv->dbus_g_proxy, "DiscoverServices", error, G_TYPE_STRING, pattern, G_TYPE_INVALID, DBUS_TYPE_G_STRING_VARIANT_HASHTABLE, &ret, G_TYPE_INVALID)) {
		return NULL;
	}

	return ret;
}

/* dict GetProperties() */
GHashTable *device_get_properties(Device *self, GError **error)
{
	g_assert(self != NULL);

	GHashTable *ret;

	if (!dbus_g_proxy_call(self->priv->dbus_g_proxy, "GetProperties", error, G_TYPE_INVALID, DBUS_TYPE_G_STRING_VARIANT_HASHTABLE, &ret, G_TYPE_INVALID)) {
		return NULL;
	}

	return ret;
}

/* array{object} ListNodes() */
GPtrArray *device_list_nodes(Device *self, GError **error)
{
	g_assert(self != NULL);

	GPtrArray *ret;

	if (!dbus_g_proxy_call(self->priv->dbus_g_proxy, "ListNodes", error, G_TYPE_INVALID, DBUS_TYPE_G_OBJECT_ARRAY, &ret, G_TYPE_INVALID)) {
		return NULL;
	}

	return ret;
}

/* void RemoveNode(object node) */
void device_remove_node(Device *self, const gchar *node, GError **error)
{
	g_assert(self != NULL);

	dbus_g_proxy_call(self->priv->dbus_g_proxy, "RemoveNode", error, DBUS_TYPE_G_OBJECT_PATH, node, G_TYPE_INVALID, G_TYPE_INVALID);
}

/* void SetProperty(string name, variant value) */
void device_set_property(Device *self, const gchar *name, const GValue *value, GError **error)
{
	g_assert(self != NULL);

	dbus_g_proxy_call(self->priv->dbus_g_proxy, "SetProperty", error, G_TYPE_STRING, name, G_TYPE_VALUE, value, G_TYPE_INVALID, G_TYPE_INVALID);
}

/* Signals handlers */
static void disconnect_requested_handler(DBusGProxy *dbus_g_proxy, gpointer data)
{
	Device *self = DEVICE(data);
	g_signal_emit(self, signals[DISCONNECT_REQUESTED], 0);
}

static void node_created_handler(DBusGProxy *dbus_g_proxy, const gchar *node, gpointer data)
{
	Device *self = DEVICE(data);
	g_signal_emit(self, signals[NODE_CREATED], 0, node);
}

static void node_removed_handler(DBusGProxy *dbus_g_proxy, const gchar *node, gpointer data)
{
	Device *self = DEVICE(data);
	g_signal_emit(self, signals[NODE_REMOVED], 0, node);
}

static void property_changed_handler(DBusGProxy *dbus_g_proxy, const gchar *name, const GValue *value, gpointer data)
{
	Device *self = DEVICE(data);
	g_signal_emit(self, signals[PROPERTY_CHANGED], 0, name, value);
}
