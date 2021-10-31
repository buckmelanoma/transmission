/******************************************************************************
 * Copyright (c) Transmission authors and contributors
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *****************************************************************************/

#include <string>

#include <glibmm.h>
#include <glibmm/i18n.h>
#include <gtkmm.h>

#include <libtransmission/transmission.h>
#include <libtransmission/utils.h>
#include <libtransmission/version.h>

#include "application.h"
#include "conf.h"
#include "notify.h"
#include "util.h"

#define MY_CONFIG_NAME "transmission"
#define MY_READABLE_NAME "transmission-gtk"

namespace
{

Glib::OptionEntry create_option_entry(Glib::ustring const& long_name, gchar short_name, Glib::ustring const& description)
{
    Glib::OptionEntry entry;
    entry.set_long_name(long_name);
    entry.set_short_name(short_name);
    entry.set_description(description);
    return entry;
}

} // namespace

int main(int argc, char** argv)
{
    /* init i18n */
    setlocale(LC_ALL, "");
    bindtextdomain(MY_READABLE_NAME, TRANSMISSIONLOCALEDIR);
    bind_textdomain_codeset(MY_READABLE_NAME, "UTF-8");
    textdomain(MY_READABLE_NAME);

    /* init glib/gtk */
    Glib::init();
    Glib::set_application_name(_("Transmission"));

    /* default settings */
    std::string config_dir = tr_getDefaultConfigDir(MY_CONFIG_NAME);
    bool show_version = false;
    bool start_paused = false;
    bool is_iconified = false;

    /* parse the command line */
    auto const config_dir_option = create_option_entry("config-dir", 'g', _("Where to look for configuration files"));
    auto const paused_option = create_option_entry("paused", 'p', _("Start with all torrents paused"));
    auto const minimized_option = create_option_entry("minimized", 'm', _("Start minimized in notification area"));
    auto const version_option = create_option_entry("version", 'v', _("Show version number and exit"));

    Glib::OptionGroup main_group({}, {});
    main_group.add_entry_filename(config_dir_option, config_dir);
    main_group.add_entry(paused_option, start_paused);
    main_group.add_entry(minimized_option, is_iconified);
    main_group.add_entry(version_option, show_version);

    Glib::OptionContext option_context(_("[torrent files or urls]"));
    option_context.set_main_group(main_group);
    Gtk::Main::add_gtk_option_group(option_context);
    option_context.set_translation_domain(GETTEXT_PACKAGE);

    try
    {
        option_context.parse(argc, argv);
    }
    catch (Glib::OptionError const& e)
    {
        g_print(_("%s\nRun '%s --help' to see a full list of available command line options.\n"), e.what().c_str(), argv[0]);
        return 1;
    }

    /* handle the trivial "version" option */
    if (show_version)
    {
        fprintf(stderr, "%s %s\n", MY_READABLE_NAME, LONG_VERSION_STRING);
        return 0;
    }

    Gtk::Window::set_default_icon_name(MY_CONFIG_NAME);

    /* init the unit formatters */
    tr_formatter_mem_init(mem_K, _(mem_K_str), _(mem_M_str), _(mem_G_str), _(mem_T_str));
    tr_formatter_size_init(disk_K, _(disk_K_str), _(disk_M_str), _(disk_G_str), _(disk_T_str));
    tr_formatter_speed_init(speed_K, _(speed_K_str), _(speed_M_str), _(speed_G_str), _(speed_T_str));

    /* set up the config dir */
    gtr_pref_init(config_dir);
    g_mkdir_with_parents(config_dir.c_str(), 0755);

    /* init notifications */
    gtr_notify_init();

    /* init the application for the specified config dir */
    return Application(config_dir, start_paused, is_iconified).run(argc, argv);
}
