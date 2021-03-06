/*
 *  Copyright 2020 Jesse Lentz
 *
 *  This file is part of iwgtk.
 *
 *  iwgtk is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  iwgtk is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with iwgtk.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "iwgtk.h"

static const CallbackMessages scan_messages = {
    NULL,
    "Error scanning",
    detailed_errors_standard
};

void scan_button_clicked(GtkButton *button, Station *station) {
    g_dbus_proxy_call(
	station->proxy,
	"Scan",
	NULL,
	G_DBUS_CALL_FLAGS_NONE,
	-1,
	NULL,
	(GAsyncReadyCallback) validation_callback,
	(gpointer) &scan_messages);
}

void network_remove_callback(GtkWidget *network, GtkWidget *network_table) {
    gtk_container_remove(GTK_CONTAINER(network_table), network);
}

void scan_button_update(GDBusProxy *proxy, GVariant *properties, gchar **invalidated_properties, Station *station) {
    GVariant *scanning_var;

    scanning_var = lookup_property(properties, "Scanning");
    if (scanning_var) {
	gboolean scanning;
	scanning = g_variant_get_boolean(scanning_var);

	gtk_container_remove(
		GTK_CONTAINER(station->scan_button),
		gtk_bin_get_child(GTK_BIN(station->scan_button)));

	scan_button_set_child(station, scanning);

	if (!scanning) {
	    /*
	     * The "Scanning" property has just changed from true to false.
	     * Therefore, a scan has been completed and the network list must be updated.
	     */

	    gtk_container_foreach(GTK_CONTAINER(station->networks), (GtkCallback) network_remove_callback, (gpointer) station->networks);
	    populate_network_list(station);
	}

	g_variant_unref(scanning_var);
    }
}

void scan_button_set_child(Station *station, gboolean scanning) {
    gtk_widget_set_sensitive(station->scan_button, !scanning);

    if (scanning) {
	gtk_container_add(GTK_CONTAINER(station->scan_button), station->scan_widget_scanning);
    }
    else {
	gtk_container_add(GTK_CONTAINER(station->scan_button), station->scan_widget_idle);
    }
}

GtkWidget* scan_button_new(Station *station) {
    GtkWidget *spinner;

    station->scan_button = gtk_button_new();
    station->scan_widget_idle = gtk_label_new("Scan");
    gtk_widget_show(station->scan_widget_idle);
    g_object_ref(station->scan_widget_idle);

    station->scan_widget_scanning = label_with_spinner("Scanning");
    g_object_ref(station->scan_widget_scanning);

    {
	GVariant *scanning_var;
	gboolean scanning;

	scanning_var = g_dbus_proxy_get_cached_property(station->proxy, "Scanning");
	scanning = g_variant_get_boolean(scanning_var);
	scan_button_set_child(station, scanning);
	g_variant_unref(scanning_var);
    }

    station->handler_scan = g_signal_connect(station->proxy, "g-properties-changed", G_CALLBACK(scan_button_update), (gpointer) station);
    g_signal_connect(station->scan_button, "clicked", G_CALLBACK(scan_button_clicked), (gpointer) station);

    return station->scan_button;
}

void station_set(Station *station) {
    GVariant *state_var;
    GVariant *connected_network_var;
    const gchar *state;

    state_var = g_dbus_proxy_get_cached_property(station->proxy, "State");
    state = g_variant_get_string(state_var, NULL);
    gtk_label_set_text(GTK_LABEL(station->device->status), state);
    g_variant_unref(state_var);

    connected_network_var = g_dbus_proxy_get_cached_property(station->proxy, "ConnectedNetwork");
    if (connected_network_var) {
	const gchar *network_path;
	Network *network;

	network_path = g_variant_get_string(connected_network_var, NULL);
	network = network_lookup(station, network_path);
	g_variant_unref(connected_network_var);

	if (network) {
	    network_set(network);
	}
    }
}

Station* station_add(Window *window, GDBusObject *object, GDBusProxy *proxy) {
    Station *station;

    station = malloc(sizeof(Station));
    station->proxy = proxy;

    station->scan_button = scan_button_new(station);
    gtk_widget_show_all(station->scan_button);

    station->networks = gtk_grid_new();
    g_object_ref_sink(station->networks);
    gtk_grid_set_column_spacing(GTK_GRID(station->networks), 10);
    gtk_grid_set_row_spacing(GTK_GRID(station->networks), 10);

    gtk_widget_set_margin_start(station->networks, 5);
    gtk_widget_set_margin_end(station->networks, 5);
    gtk_widget_set_margin_bottom(station->networks, 5);

    populate_network_list(station);
    gtk_widget_show_all(station->networks);

    station->handler_update = g_signal_connect_swapped(proxy, "g-properties-changed", G_CALLBACK(station_set), (gpointer) station);

    couple_register(window, DEVICE_STATION, 1, station, object);
    return station;
}

void station_remove(Window *window, Station *station) {
    g_object_unref(station->scan_widget_idle);
    g_object_unref(station->scan_widget_scanning);
    g_object_unref(station->networks);
    couple_unregister(window, DEVICE_STATION, 1, station);

    g_signal_handler_disconnect(station->proxy, station->handler_update);
    g_signal_handler_disconnect(station->proxy, station->handler_scan);
    free(station);
}

void bind_device_station(Device *device, Station *station) {
    station->device = device;

    gtk_grid_attach(GTK_GRID(device->table), station->scan_button, 4, 0, 1, 1);
    gtk_widget_set_halign(station->scan_button, GTK_ALIGN_FILL);

    gtk_box_pack_start(GTK_BOX(device->master), station->networks, TRUE, TRUE, 0);

    station_set(station);
}

void unbind_device_station(Device *device, Station *station) {
    station->device = NULL;
    gtk_container_remove(GTK_CONTAINER(device->table), station->scan_button);
    gtk_container_remove(GTK_CONTAINER(device->master), station->networks);
}

void insert_separator(Station *station) {
    GtkWidget *separator;

    separator = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_grid_attach(GTK_GRID(station->networks), separator, 0, station->i ++, 5, 1);
}

void populate_network_list(Station *station) {
    g_dbus_proxy_call(
	station->proxy,
	"GetOrderedNetworks",
	NULL,
	G_DBUS_CALL_FLAGS_NONE,
	-1,
	NULL,
	(GAsyncReadyCallback) get_networks_callback,
	(gpointer) station);
}

void get_networks_callback(GDBusProxy *proxy, GAsyncResult *res, Station *station) {
    GVariant *ordered_networks;
    GError *err;

    err = NULL;
    ordered_networks = g_dbus_proxy_call_finish(proxy, res, &err);

    if (ordered_networks) {
	GVariantIter *iter;
	gchar *network_path;
	gint16 signal_strength;

	g_variant_get(ordered_networks, "(a(on))", &iter);
	station->i = 0;
	while (g_variant_iter_next(iter, "(on)", &network_path, &signal_strength)) {
	    Network *network;

	    network = network_lookup(station, network_path);
	    if (network) {
		bind_station_network(station, network, signal_strength, station->i ++);
	    }
	    else {
		fprintf(stderr, "Error: Network '%s' not found\n", network_path);
	    }
	    g_free(network_path);
	}

	if (station->i > 0) {
	    insert_separator(station);
	}

	g_variant_iter_free(iter);
	g_variant_unref(ordered_networks);

	gtk_widget_show_all(station->networks);
    }
    else {
	fprintf(stderr, "Error retrieving network list: %s\n", err->message);
	g_error_free(err);
    }

    g_dbus_proxy_call(
	proxy,
	"GetHiddenAccessPoints",
	NULL,
	G_DBUS_CALL_FLAGS_NONE,
	-1,
	NULL,
	(GAsyncReadyCallback) get_hidden_networks_callback,
	(gpointer) station);
}

void get_hidden_networks_callback(GDBusProxy *proxy, GAsyncResult *res, Station *station) {
    GVariant *ordered_networks;
    GError *err;

    err = NULL;
    ordered_networks = g_dbus_proxy_call_finish(proxy, res, &err);

    if (ordered_networks) {
	int n;
	GVariantIter *iter;
	gchar *address;
	gint16 signal_strength;
	gchar *type;

	n = 0;
	g_variant_get(ordered_networks, "(a(sns))", &iter);
	while (g_variant_iter_next(iter, "(sns)", &address, &signal_strength, &type)) {
	    bind_station_network_hidden(station, address, type, signal_strength, station->i ++);
	    g_free(address);
	    g_free(type);
	    n ++;
	}

	if (n != 0) {
	    GtkWidget *connect_button;

	    connect_button = gtk_button_new_with_label("Connect");
	    g_signal_connect_swapped(connect_button, "clicked", G_CALLBACK(hidden_ssid_dialog), (gpointer) station);

	    gtk_grid_attach(GTK_GRID(station->networks), connect_button, 4, station->i - n, 1, n);
	    gtk_widget_set_halign(connect_button, GTK_ALIGN_FILL);
	    gtk_widget_set_valign(connect_button, GTK_ALIGN_FILL);

	    insert_separator(station);
	}

	g_variant_iter_free(iter);
	g_variant_unref(ordered_networks);

	gtk_widget_show_all(station->networks);
    }
    else {
	fprintf(stderr, "Error retrieving hidden network list: %s\n", err->message);
	g_error_free(err);
    }
}
