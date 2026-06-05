
#include <pybind11/pybind11.h>

#include "../public/steam/steam_api.h"
#include "../public/steam/steam_api_flat.h"
#include "../public/steam/steam_gameserver.h" // for EServerMode enum

namespace py = pybind11;

PYBIND11_MODULE(steamworks, m) {
  m.def("init", &SteamAPI_Init);
  m.def("shutdown", &SteamAPI_Shutdown);
}
