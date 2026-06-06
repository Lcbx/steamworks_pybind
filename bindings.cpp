
#include <pybind11/pybind11.h>
#include <string>

#include "../public/steam/steam_api.h"
#include "../public/steam/steam_api_flat.h"
#include "../public/steam/steam_gameserver.h" // for EServerMode enum

namespace py = pybind11;


template <size_t N>
std::string get_char_array(const char (&buf)[N]) {
	return std::string(buf);
}

template <size_t N>
void set_char_array(char (&buf)[N], const std::string& s) {
	std::strncpy(buf, s.c_str(), N - 1);
	buf[N - 1] = '\0';
}


PYBIND11_MODULE(steamworks, m) {
	m.def("init", &SteamAPI_Init);
	m.def("shutdown", &SteamAPI_Shutdown);

	py::class_<CSteamID>(m, "SteamID")
		.def(py::init<uint64>())
		.def("__int__", &CSteamID::ConvertToUint64)
		.def("__repr__", [](const CSteamID& id) { return "<CSteamID " + std::to_string(id.ConvertToUint64()) + ">";})
		.def("__eq__", [](const CSteamID& a, const CSteamID& b) { return a.ConvertToUint64() == b.ConvertToUint64(); })
	;

	py::class_<CGameID> game_id(m, "GameID");
	py::enum_<CGameID::EGameIDType>(game_id, "EGameIDType")
		.value("App", CGameID::k_EGameIDTypeApp)
		.value("GameMod", CGameID::k_EGameIDTypeGameMod)
		.value("Shortcut", CGameID::k_EGameIDTypeShortcut)
		.value("P2P", CGameID::k_EGameIDTypeP2P);
	game_id
		.def(py::init<>())
		.def(py::init<uint64>(), py::arg("game_id"))
    	.def(py::init<uint32>(), py::arg("app_id"))
		.def(py::init<uint32, uint32, CGameID::EGameIDType>(),
			py::arg("app_id"), py::arg("mod_id"), py::arg("game_id_type"))
		.def(py::init<uint32, uint32, CGameID::EGameIDType>())
		.def("set", &CGameID::Set, py::arg("game_id"))
		.def("__int__", &CGameID::ToUint64)
		.def("__repr__", [](const CGameID& x) { return "<CGameID " + std::to_string(x.ToUint64()) + ">"; })
		.def("__eq__", [](const CGameID& a, const CGameID& b) { return a.ToUint64() == b.ToUint64(); })
	;

	/* __PROCEDURALY_GENERATED__ */ 	

 	py::class_<MatchMakingKeyValuePair_t> matchmakingkeyvaluepair_t(m, "MatchMakingKeyValuePair");
 	matchmakingkeyvaluepair_t.def(py::init<>())
 		.def_property("szKey",
			[](const MatchMakingKeyValuePair_t& x) { return get_char_array(x.m_szKey); },
			[](MatchMakingKeyValuePair_t& x, const std::string& s) { return set_char_array(x.m_szKey, s); })
 		.def_property("szValue",
			[](const MatchMakingKeyValuePair_t& x) { return get_char_array(x.m_szValue); },
			[](MatchMakingKeyValuePair_t& x, const std::string& s) { return set_char_array(x.m_szValue, s); })
 	;
 	py::class_<FriendGameInfo_t> friendgameinfo_t(m, "FriendGameInfo");
 	friendgameinfo_t.def(py::init<>())
 		.def_readwrite("gameID", &FriendGameInfo_t::m_gameID)
 		.def_readwrite("unGameIP", &FriendGameInfo_t::m_unGameIP)
 		.def_readwrite("usGamePort", &FriendGameInfo_t::m_usGamePort)
 		.def_readwrite("usQueryPort", &FriendGameInfo_t::m_usQueryPort)
 		.def_readwrite("steamIDLobby", &FriendGameInfo_t::m_steamIDLobby)
 	;
 	
}
