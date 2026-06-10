#include "common.h"


void bind_init(py::module_& m){
	m.def("init", &SteamAPI_Init);
	m.def("shutdown", &SteamAPI_Shutdown);

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

class PyCallbackHolder {
public:
	explicit PyCallbackHolder(py::dict callbacks) : callbacks(std::move(callbacks)) {}
	py::dict callbacks;
};

class PingResponseAdapter : public ISteamMatchmakingPingResponse, public PyCallbackHolder {
public:
	using PyCallbackHolder::PyCallbackHolder;
	
	void ServerResponded(gameserveritem_t& server) override {
		callbacks["ServerResponded"](server);
	}
	void ServerFailedToRespond() override {
		callbacks["ServerFailedToRespond"]();
	}
};

class ServerListResponseAdapter : public ISteamMatchmakingServerListResponse, public PyCallbackHolder {
public:
	using PyCallbackHolder::PyCallbackHolder;
	void ServerResponded(HServerListRequest hRequest, int iServer) override {
		callbacks["ServerResponded"](hRequest, iServer);
	}
	void ServerFailedToRespond(HServerListRequest hRequest, int iServer) override {
		callbacks["ServerFailedToRespond"](hRequest, iServer);
	}
	void RefreshComplete(HServerListRequest hRequest, EMatchMakingServerResponse response) override {
		callbacks["RefreshComplete"](hRequest, response);
	}
};


class PlayersResponseAdapter : public ISteamMatchmakingPlayersResponse, public PyCallbackHolder {
public:
	using PyCallbackHolder::PyCallbackHolder;
	void AddPlayerToList(const char* pchName, int nScore, float flTimePlayed) override {
		callbacks["AddPlayerToList"](pchName, nScore, flTimePlayed);
	}
	void PlayersFailedToRespond() override {
		callbacks["PlayersFailedToRespond"]();
	}
	void PlayersRefreshComplete() override {
		callbacks["PlayersRefreshComplete"]();
	}
};


class RulesResponseAdapter : public ISteamMatchmakingRulesResponse, public PyCallbackHolder {
public:
	using PyCallbackHolder::PyCallbackHolder;
	void RulesResponded(const char* pchRule, const char* pchValue) override {
		callbacks["RulesResponded"](pchRule, pchValue);
	}
	void RulesFailedToRespond() override {
		callbacks["RulesFailedToRespond"]();
	}
	void RulesRefreshComplete() override {
		callbacks["RulesRefreshComplete"]();
	}
};

void bind_response_adapters(py::module_& m){
	py::class_<PingResponseAdapter, ISteamMatchmakingPingResponse>(m, "SteamMatchmakingPingResponse")
		.def_property_readonly("ptr", [](PingResponseAdapter& self) { return reinterpret_cast<uintptr_t>(&self); })
		.def(py::init<py::dict>())
	;

	py::class_<ServerListResponseAdapter, ISteamMatchmakingServerListResponse>(m, "SteamMatchmakingServerListResponse")
		.def_property_readonly("ptr", [](ServerListResponseAdapter& self) { return reinterpret_cast<uintptr_t>(&self); })
		.def(py::init<py::dict>())
	;

	py::class_<PlayersResponseAdapter, ISteamMatchmakingPlayersResponse>(m, "SteamMatchmakingPlayersResponse")
		.def_property_readonly("ptr", [](PlayersResponseAdapter& self) { return reinterpret_cast<uintptr_t>(&self); })
		.def(py::init<py::dict>())
	;

	py::class_<RulesResponseAdapter, ISteamMatchmakingRulesResponse>(m, "SteamMatchmakingRulesResponse")
		.def_property_readonly("ptr", [](RulesResponseAdapter& self) { return reinterpret_cast<uintptr_t>(&self); })
		.def(py::init<py::dict>())
	;
}