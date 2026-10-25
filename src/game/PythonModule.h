/******************************************************************************
 * Copyright (c) 2026 Sims 4 mods developers
 *****************************************************************************/

#pragma once

#include <common/Result.h>

namespace ts4_mod::youtube_tv
{

/// The name of the Python module that the DLL adds to the interpreter of the game.
inline constexpr const char* k_PythonModuleName = "ts4_youtube_tv_native";

/// The Python module `ts4_youtube_tv_native` for the script mod. Its functions are the functions of the C interface
/// of the bridge: `api_version`, `play`, `stop`, `stop_all`, `set_speed`, `set_transform`, `set_screen_key` and
/// `last_error`. The game Python has no `ctypes`, so the script mod cannot call the C interface directly.
namespace python_module
{

/// Add the module to `sys.modules`. Call it on the main thread. The result is true when the module is in
/// `sys.modules`, false when the interpreter is not ready yet (call it again later), or an error.
[[nodiscard]] ts4::Result<bool> Register();

/// Remove the module from `sys.modules` if the interpreter still runs. Call it on the main thread before the DLL
/// unloads. The game Python must not call a function of the module after the DLL unloads.
void Unregister();

} // namespace python_module

} // namespace ts4_mod::youtube_tv
