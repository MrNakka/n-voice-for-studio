/*
N Voice for Studio - AI Audio Engine for OBS
Copyright (C) 2026 N Voice

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program. If not, see <https://www.gnu.org/licenses/>
*/

#include "obs/nvoice_filter.hpp"
#include "ui/dashboard_dock.hpp"

#include <obs-frontend-api.h>
#include <obs-module.h>
#include <plugin-support.h>

#include <QMainWindow>
#include <QPointer>

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE(PLUGIN_NAME, "en-US")

namespace {

constexpr const char *kDockId = "nvoice-studio-dashboard";
QPointer<nvoice::ui::DashboardDock> g_dock;

void onFrontendEvent(enum obs_frontend_event event, void *)
{
	// Stop UI polling before OBS tears down sources and the main window.
	if (event == OBS_FRONTEND_EVENT_EXIT && g_dock)
		g_dock->shutdown();
}

} // namespace

MODULE_EXPORT const char *obs_module_description(void)
{
	return obs_module_text("NVoice.Description");
}

bool obs_module_load(void)
{
	nvoice::obs::registerAudioFilter();

	auto *mainWindow = static_cast<QMainWindow *>(obs_frontend_get_main_window());
	g_dock = new nvoice::ui::DashboardDock(mainWindow);
	if (!obs_frontend_add_dock_by_id(kDockId, obs_module_text("NVoice.Dock.Title"), g_dock)) {
		obs_log(LOG_WARNING, "failed to add dock '%s'", kDockId);
		delete g_dock;
	}
	obs_frontend_add_event_callback(onFrontendEvent, nullptr);

	obs_log(LOG_INFO, "plugin loaded successfully (version %s)", PLUGIN_VERSION);
	return true;
}

void obs_module_unload(void)
{
	obs_frontend_remove_event_callback(onFrontendEvent, nullptr);
	obs_log(LOG_INFO, "plugin unloaded");
}
