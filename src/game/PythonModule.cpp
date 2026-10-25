/******************************************************************************
 * Copyright (c) 2026 Sims 4 mods developers
 *****************************************************************************/

#include "PythonModule.h"

#include <cstdint>

#include <array>
#include <exception>
#include <optional>
#include <string>

#include <Windows.h>
#include <mod_game/Python.h>

#include "game/Bridge.h"
#include "game/YoutubeTvMod.h"

namespace ts4_mod::youtube_tv::python_module
{

namespace
{

// The method table of the C API of Python 3.7. The layout is the public C API of Python, not a layout of the game.
using PyCFunction = PyObject* (*)(PyObject * self, PyObject* args);

struct PyMethodDef
{
	const char* name;
	PyCFunction function;
	int flags;
	const char* doc;
};

constexpr int k_MethVarArgs = 0x0001;
constexpr int k_MethNoArgs = 0x0004;

/// The exports of the interpreter that `PythonApi` of common does not have.
struct ModuleExports
{
	PyObject* (*PyModule_New)(const char*) = nullptr;
	PyObject* (*PyCFunction_NewEx)(PyMethodDef*, PyObject*, PyObject*) = nullptr;
	int (*PyObject_SetAttrString)(PyObject*, const char*, PyObject*) = nullptr;
	PyObject* (*PyImport_GetModuleDict)() = nullptr;
	int (*PyDict_SetItemString)(PyObject*, const char*, PyObject*) = nullptr;
	int (*PyDict_DelItemString)(PyObject*, const char*) = nullptr;
	int (*PyArg_ParseTuple)(PyObject*, const char*, ...) = nullptr;
	PyObject* (*Py_BuildValue)(const char*, ...) = nullptr;
	void (*PyErr_SetString)(PyObject*, const char*) = nullptr;
	PyObject** PyExc_RuntimeError = nullptr;
};

/// The state of the module. The main thread is the only thread that changes it.
struct ModuleState
{
	std::optional<PythonApi> api;
	ModuleExports exports;
	bool registered = false;
};

ModuleState& State()
{
	static ModuleState state;
	return state;
}

template <typename Function>
bool Resolve(HMODULE module, const char* name, Function& function)
{
	function = reinterpret_cast<Function>(::GetProcAddress(module, name));
	return function != nullptr;
}

[[nodiscard]] ts4::Result<ModuleExports> FindExports(HMODULE module)
{
	ModuleExports exports;
	std::string missing;
	const auto need = [&](const char* name, auto& function) {
		if (!Resolve(module, name, function))
		{
			missing += missing.empty() ? name : std::string(", ") + name;
		}
	};
	need("PyModule_New", exports.PyModule_New);
	need("PyCFunction_NewEx", exports.PyCFunction_NewEx);
	need("PyObject_SetAttrString", exports.PyObject_SetAttrString);
	need("PyImport_GetModuleDict", exports.PyImport_GetModuleDict);
	need("PyDict_SetItemString", exports.PyDict_SetItemString);
	need("PyDict_DelItemString", exports.PyDict_DelItemString);
	need("PyArg_ParseTuple", exports.PyArg_ParseTuple);
	need("Py_BuildValue", exports.Py_BuildValue);
	need("PyErr_SetString", exports.PyErr_SetString);
	need("PyExc_RuntimeError", exports.PyExc_RuntimeError);
	if (!missing.empty())
	{
		return ts4::Error {"the interpreter has no " + missing};
	}
	return exports;
}

/// Run the body of a module function. An exception of C++ becomes a RuntimeError of Python.
template <typename Body>
PyObject* Guarded(Body body)
{
	const auto& exports = State().exports;
	try
	{
		return body(exports);
	}
	catch (const std::exception& exception)
	{
		exports.PyErr_SetString(*exports.PyExc_RuntimeError, exception.what());
	}
	catch (...)
	{
		exports.PyErr_SetString(*exports.PyExc_RuntimeError, "an unknown exception in youtube_tv.dll");
	}
	return nullptr;
}

PyObject* ApiVersion(PyObject*, PyObject*)
{
	return Guarded([](const ModuleExports& py) { return py.Py_BuildValue("I", YoutubeTvApiVersion()); });
}

PyObject* Play(PyObject*, PyObject* args)
{
	return Guarded([args](const ModuleExports& py) -> PyObject* {
		unsigned long long objectId = 0;
		const char* url = nullptr;
		std::array<float, 3> position {};
		std::array<float, 4> orientation {};
		if (py.PyArg_ParseTuple(args, "Ks(fff)(ffff):play", &objectId, &url, &position[0], &position[1], &position[2],
		                        &orientation[0], &orientation[1], &orientation[2], &orientation[3]) == 0)
		{
			return nullptr;
		}
		return py.Py_BuildValue("I", YoutubeTvPlay(objectId, url, position.data(), orientation.data()));
	});
}

PyObject* Stop(PyObject*, PyObject* args)
{
	return Guarded([args](const ModuleExports& py) -> PyObject* {
		unsigned long long objectId = 0;
		if (py.PyArg_ParseTuple(args, "K:stop", &objectId) == 0)
		{
			return nullptr;
		}
		return py.Py_BuildValue("I", YoutubeTvStop(objectId));
	});
}

PyObject* StopAll(PyObject*, PyObject*)
{
	return Guarded([](const ModuleExports& py) {
		YoutubeTvStopAll();
		return py.Py_BuildValue("");
	});
}

PyObject* SetSpeed(PyObject*, PyObject* args)
{
	return Guarded([args](const ModuleExports& py) -> PyObject* {
		unsigned int mode = 0;
		double scale = 0.0;
		unsigned int superSpeed = 0;
		if (py.PyArg_ParseTuple(args, "IdI:set_speed", &mode, &scale, &superSpeed) == 0)
		{
			return nullptr;
		}
		YoutubeTvSetSpeed(mode, scale, superSpeed);
		return py.Py_BuildValue("");
	});
}

PyObject* SetTransform(PyObject*, PyObject* args)
{
	return Guarded([args](const ModuleExports& py) -> PyObject* {
		unsigned long long objectId = 0;
		std::array<float, 3> position {};
		std::array<float, 4> orientation {};
		if (py.PyArg_ParseTuple(args, "K(fff)(ffff):set_transform", &objectId, &position[0], &position[1], &position[2],
		                        &orientation[0], &orientation[1], &orientation[2], &orientation[3]) == 0)
		{
			return nullptr;
		}
		YoutubeTvSetTransform(objectId, position.data(), orientation.data());
		return py.Py_BuildValue("");
	});
}

PyObject* SetScreenKey(PyObject*, PyObject* args)
{
	return Guarded([args](const ModuleExports& py) -> PyObject* {
		unsigned long long objectId = 0;
		unsigned long long instance = 0;
		unsigned int type = 0;
		unsigned int group = 0;
		if (py.PyArg_ParseTuple(args, "KKII:set_screen_key", &objectId, &instance, &type, &group) == 0)
		{
			return nullptr;
		}
		YoutubeTvSetScreenKey(objectId, instance, type, group);
		return py.Py_BuildValue("");
	});
}

PyObject* LastError(PyObject*, PyObject*)
{
	return Guarded([](const ModuleExports& py) {
		std::array<char, 1024> buffer {};
		YoutubeTvLastError(buffer.data(), static_cast<std::uint32_t>(buffer.size()));
		return py.Py_BuildValue("s", buffer.data());
	});
}

PyObject* Status(PyObject*, PyObject*)
{
	return Guarded([](const ModuleExports& py) -> PyObject* {
		auto* mod = ActiveYoutubeTvMod();
		if (mod == nullptr)
		{
			py.PyErr_SetString(*py.PyExc_RuntimeError, "youtube_tv.dll is not ready");
			return nullptr;
		}
		if (!mod->OnPythonThread())
		{
			py.PyErr_SetString(*py.PyExc_RuntimeError, "the call is not on the thread of the game Python");
			return nullptr;
		}
		const auto text = mod->Runtime().Status().dump();
		return py.Py_BuildValue("s", text.c_str());
	});
}

/// The functions of the module. Python keeps pointers to these rows, so the table is static.
std::array<PyMethodDef, 9> s_methods {{
    {"api_version", &ApiVersion, k_MethNoArgs, "api_version() -> int: the version of the bridge."},
    {"play", &Play, k_MethVarArgs, "play(object_id, url, (x, y, z), (x, y, z, w)) -> int: 1, or 0 and last_error()."},
    {"stop", &Stop, k_MethVarArgs, "stop(object_id) -> int: 1 if a video stopped."},
    {"stop_all", &StopAll, k_MethNoArgs, "stop_all(): stop every video."},
    {"set_speed", &SetSpeed, k_MethVarArgs, "set_speed(mode, scale, super_speed): the clock of the game."},
    {"set_transform", &SetTransform, k_MethVarArgs, "set_transform(object_id, (x, y, z), (x, y, z, w))."},
    {"set_screen_key", &SetScreenKey, k_MethVarArgs, "set_screen_key(object_id, instance, type, group)."},
    {"last_error", &LastError, k_MethNoArgs, "last_error() -> str: the error of the last call."},
    {"status", &Status, k_MethNoArgs, "status() -> str: the status of the DLL as JSON text."},
}};

} // namespace

ts4::Result<bool> Register()
{
	auto& state = State();
	if (state.registered)
	{
		return true;
	}
	const auto wideName = std::wstring(k_GamePythonModule.begin(), k_GamePythonModule.end());
	auto* module = ::GetModuleHandleW(wideName.c_str());
	if (module == nullptr)
	{
		return false;
	}
	if (!state.api)
	{
		auto api = PythonApi::FromModule(module, std::string(k_GamePythonModule));
		if (!api)
		{
			return api.GetError();
		}
		auto exports = FindExports(module);
		if (!exports)
		{
			return exports.GetError();
		}
		state.api = std::move(*api);
		state.exports = *exports;
	}
	const auto& api = *state.api;
	const auto& py = state.exports;
	if (api.Py_IsInitialized() == 0)
	{
		return false;
	}
	const GilGuard gil(api);
	auto object = PyRef::Steal(api, py.PyModule_New(k_PythonModuleName));
	if (!object)
	{
		return api.TakeError("PyModule_New");
	}
	for (auto& method : s_methods)
	{
		auto function = PyRef::Steal(api, py.PyCFunction_NewEx(&method, nullptr, nullptr));
		if (!function)
		{
			return api.TakeError(std::string("the function ") + method.name);
		}
		if (py.PyObject_SetAttrString(object.Get(), method.name, function.Get()) != 0)
		{
			return api.TakeError(std::string("the attribute ") + method.name);
		}
	}
	auto* modules = py.PyImport_GetModuleDict();
	if (modules == nullptr || py.PyDict_SetItemString(modules, k_PythonModuleName, object.Get()) != 0)
	{
		return api.TakeError("sys.modules");
	}
	state.registered = true;
	return true;
}

void Unregister()
{
	auto& state = State();
	if (!state.registered || !state.api || state.api->Py_IsInitialized() == 0)
	{
		return;
	}
	const auto& api = *state.api;
	const GilGuard gil(api);
	if (auto* modules = state.exports.PyImport_GetModuleDict(); modules != nullptr)
	{
		if (state.exports.PyDict_DelItemString(modules, k_PythonModuleName) != 0)
		{
			api.PyErr_Clear();
		}
	}
	state.registered = false;
}

} // namespace ts4_mod::youtube_tv::python_module
