#include "common.h"


void bind_init(py::module_& m){
	m.def("init", &SteamAPI_Init);
	m.def("shutdown", &SteamAPI_Shutdown);
	m.def("RunCallbacks", &SteamAPI_RunCallbacks);

	// for convenience
	m.def("to_bytes", [](py::capsule ptr, size_t size) { return py::bytes(static_cast<const char*>(ptr.get_pointer()), size); });

	py::class_<CSteamID>(m, "SteamID")
		.def(py::init<uint64>())
 		.def_property_readonly("ptr", [](CSteamID& self) { return reinterpret_cast<uintptr_t>(&self); })
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
 		.def_property_readonly("ptr", [](CGameID& self) { return reinterpret_cast<uintptr_t>(&self); })
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
}


class PingResponseAdapter : public ISteamMatchmakingPingResponse, public PyMultiCallbackHolder {
public:
	using PyMultiCallbackHolder::PyMultiCallbackHolder;
	void ServerResponded(gameserveritem_t& server) override {
		callback_dict["ServerResponded"](server);
	}
	void ServerFailedToRespond() override {
		callback_dict["ServerFailedToRespond"]();
	}
};

class ServerListResponseAdapter : public ISteamMatchmakingServerListResponse, public PyMultiCallbackHolder {
public:
	using PyMultiCallbackHolder::PyMultiCallbackHolder;
	void ServerResponded(HServerListRequest hRequest, int iServer) override {
		callback_dict["ServerResponded"](hRequest, iServer);
	}
	void ServerFailedToRespond(HServerListRequest hRequest, int iServer) override {
		callback_dict["ServerFailedToRespond"](hRequest, iServer);
	}
	void RefreshComplete(HServerListRequest hRequest, EMatchMakingServerResponse response) override {
		callback_dict["RefreshComplete"](hRequest, response);
	}
};


class PlayersResponseAdapter : public ISteamMatchmakingPlayersResponse, public PyMultiCallbackHolder {
public:
	using PyMultiCallbackHolder::PyMultiCallbackHolder;
	void AddPlayerToList(const char* pchName, int nScore, float flTimePlayed) override {
		callback_dict["AddPlayerToList"](pchName, nScore, flTimePlayed);
	}
	void PlayersFailedToRespond() override {
		callback_dict["PlayersFailedToRespond"]();
	}
	void PlayersRefreshComplete() override {
		callback_dict["PlayersRefreshComplete"]();
	}
};


class RulesResponseAdapter : public ISteamMatchmakingRulesResponse, public PyMultiCallbackHolder {
public:
	using PyMultiCallbackHolder::PyMultiCallbackHolder;
	void RulesResponded(const char* pchRule, const char* pchValue) override {
		callback_dict["RulesResponded"](pchRule, pchValue);
	}
	void RulesFailedToRespond() override {
		callback_dict["RulesFailedToRespond"]();
	}
	void RulesRefreshComplete() override {
		callback_dict["RulesRefreshComplete"]();
	}
};

void bind_response_adapters(py::module_& m){
	py::class_<PingResponseAdapter, ISteamMatchmakingPingResponse>(m, "SteamMatchmakingPingResponse")
		.def_property_readonly("ptr", [](PingResponseAdapter& self) { return reinterpret_cast<uintptr_t>(&self); })
		.def(py::init<py::dict&>())
		.def("__setitem__", &PingResponseAdapter::set_item)
	;

	py::class_<ServerListResponseAdapter, ISteamMatchmakingServerListResponse>(m, "SteamMatchmakingServerListResponse")
		.def_property_readonly("ptr", [](ServerListResponseAdapter& self) { return reinterpret_cast<uintptr_t>(&self); })
		.def(py::init<py::dict&>())
		.def("__setitem__", &ServerListResponseAdapter::set_item)
	;

	py::class_<PlayersResponseAdapter, ISteamMatchmakingPlayersResponse>(m, "SteamMatchmakingPlayersResponse")
		.def_property_readonly("ptr", [](PlayersResponseAdapter& self) { return reinterpret_cast<uintptr_t>(&self); })
		.def(py::init<py::dict&>())
		.def("__setitem__", &PlayersResponseAdapter::set_item)
	;

	py::class_<RulesResponseAdapter, ISteamMatchmakingRulesResponse>(m, "SteamMatchmakingRulesResponse")
		.def_property_readonly("ptr", [](RulesResponseAdapter& self) { return reinterpret_cast<uintptr_t>(&self); })
		.def(py::init<py::dict&>())
		.def("__setitem__", &RulesResponseAdapter::set_item)
	;
}