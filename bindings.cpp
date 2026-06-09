
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <string>
#include <algorithm>

//#include "../public/steam/steam_api.h"
#include "../public/steam/steam_api_flat.h"
//#include "../public/steam/steam_gameserver.h" // EServerMode enum
#include "../public/steam/steamnetworkingfakeip.h" // SteamNetworkingFakeIPResult
//#include "../public/steam/isteamnetworkingutils.h"
//#include "../public/steam/matchmakingtypes.h"
//#include "../public/steam/isteammatchmaking.h"

namespace py = pybind11;


// accessors & conversions

template <size_t N>
std::string get_char_array(const char (&buf)[N]) {
	return std::string(buf);
}
template <size_t N>
void set_char_array(char (&buf)[N], const std::string& s) {
	std::strncpy(buf, s.c_str(), N - 1);
	buf[N - 1] = '\0';
}

/*
template <typename T, size_t N>
std::vector<T> get_fixed_array(const T (&arr)[N]) {
	return std::vector<T>(arr, arr + N);
}
template <typename T, size_t N>
void set_fixed_array(T (&arr)[N], const std::vector<T>& values) {
	if (values.size() != N)
		throw py::value_error("Expected array of length " + std::to_string(N) + ", got " + std::to_string(values.size()) );
	std::copy(values.begin(), values.end(), arr);
}
*/

template <typename T, size_t N>
struct FixedArrayView {
	T (&data)[N];

	T get(int32 i) const {
		if (i < 0) i += N; 
		if ( (size_t)i >= N) throw py::index_error();
		return data[i];
	}

	void set(int32 i, const T& value) {
		if (i < 0) i += N;
		if ( (size_t)i >= N) throw py::index_error();
		data[i] = value;
	}

	size_t size() const {
		return N;
	}

	std::vector<T> to_list() const {
		return std::vector<T>(data, data + N);
	}
};

template <typename T, size_t N>
void bind_fixed_array_view(py::module_& m, const char* name) {
	py::class_<FixedArrayView<T, N>>(m, name)
		.def("__len__", &FixedArrayView<T, N>::size)
		.def("__getitem__", &FixedArrayView<T, N>::get)
		.def("__setitem__", &FixedArrayView<T, N>::set)
		.def("__iter__", [](FixedArrayView<T, N>& self) {
			return py::make_iterator(self.data, self.data + N);
		}, py::keep_alive<0, 1>())
		.def("to_list", &FixedArrayView<T, N>::to_list);
}

namespace pybind11 {
namespace detail {
	template <>
	struct type_caster<SteamParamStringArray_t> {
	public:
		PYBIND11_TYPE_CASTER(SteamParamStringArray_t, _("Sequence[str]"));

		std::vector<std::string> storage;
		std::vector<const char*> ptrs;

		bool load(handle src, bool) {
			if (!py::isinstance<py::sequence>(src)) {
				return false;
			}

			py::sequence seq = py::reinterpret_borrow<py::sequence>(src);

			storage.clear();
			storage.reserve(py::len(seq));

			for (py::handle item : seq) {
				storage.emplace_back(py::cast<std::string>(item));
			}

			ptrs.clear();
			ptrs.reserve(storage.size());

			for (const std::string& s : storage) {
				ptrs.push_back(s.c_str());
			}

			value.m_ppStrings = ptrs.empty() ? nullptr : ptrs.data();
			value.m_nNumStrings = static_cast<int32>(ptrs.size());

			return true;
		}

		static handle cast(
			const SteamParamStringArray_t& src,
			return_value_policy,
			handle
		) {
			py::list out;

			if (!src.m_ppStrings || src.m_nNumStrings <= 0) {
				return out.release();
			}

			for (int32 i = 0; i < src.m_nNumStrings; ++i) {
				out.append(src.m_ppStrings[i] ? src.m_ppStrings[i] : "");
			}

			return out.release();
		}
	};
}}


// for SteamNetworkingMessage_t
template <typename T>
struct ReleaseDeleter {
	void operator()(T* ptr) const noexcept {
		if (ptr) {
			ptr->Release();
		}
	}
};

template <typename T>
using ReleasePtr = std::unique_ptr<T, ReleaseDeleter<T>>;


// definitions

struct SteamDatagramHostedAddress {
	int m_cbSize;
	char m_data[128];
};

struct SteamDatagramGameCoordinatorServerLogin 
{
	SteamNetworkingIdentity m_identity;
	SteamDatagramHostedAddress m_routing;
	AppId_t m_nAppID;
	RTime32 m_rtime;
	int m_cbAppData;
	char m_appData[2048];
};

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



// module

PYBIND11_MODULE(steamworks, m) {
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


	/* __PROCEDURALY_GENERATED__ */ 	

 	py::class_<SteamServersConnected_t> _steamserversconnected_t(m, "SteamServersConnected");
 	_steamserversconnected_t
 		.def_property_readonly("ptr", [](SteamServersConnected_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 	;
 	py::class_<SteamServerConnectFailure_t> _steamserverconnectfailure_t(m, "SteamServerConnectFailure");
 	_steamserverconnectfailure_t
 		.def_property_readonly("ptr", [](SteamServerConnectFailure_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("eResult", &SteamServerConnectFailure_t::m_eResult)
 		.def_readwrite("bStillRetrying", &SteamServerConnectFailure_t::m_bStillRetrying)
 	;
 	py::class_<SteamServersDisconnected_t> _steamserversdisconnected_t(m, "SteamServersDisconnected");
 	_steamserversdisconnected_t
 		.def_property_readonly("ptr", [](SteamServersDisconnected_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("eResult", &SteamServersDisconnected_t::m_eResult)
 	;
 	py::class_<ClientGameServerDeny_t> _clientgameserverdeny_t(m, "ClientGameServerDeny");
 	_clientgameserverdeny_t
 		.def_property_readonly("ptr", [](ClientGameServerDeny_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("uAppID", &ClientGameServerDeny_t::m_uAppID)
 		.def_readwrite("unGameServerIP", &ClientGameServerDeny_t::m_unGameServerIP)
 		.def_readwrite("usGameServerPort", &ClientGameServerDeny_t::m_usGameServerPort)
 		.def_readwrite("bSecure", &ClientGameServerDeny_t::m_bSecure)
 		.def_readwrite("uReason", &ClientGameServerDeny_t::m_uReason)
 	;
 	py::class_<IPCFailure_t> _ipcfailure_t(m, "IPCFailure");
 	_ipcfailure_t
 		.def_property_readonly("ptr", [](IPCFailure_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("eFailureType", &IPCFailure_t::m_eFailureType)
 	;
 	py::class_<LicensesUpdated_t> _licensesupdated_t(m, "LicensesUpdated");
 	_licensesupdated_t
 		.def_property_readonly("ptr", [](LicensesUpdated_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 	;
 	py::class_<ValidateAuthTicketResponse_t> _validateauthticketresponse_t(m, "ValidateAuthTicketResponse");
 	_validateauthticketresponse_t
 		.def_property_readonly("ptr", [](ValidateAuthTicketResponse_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("SteamID", &ValidateAuthTicketResponse_t::m_SteamID)
 		.def_readwrite("eAuthSessionResponse", &ValidateAuthTicketResponse_t::m_eAuthSessionResponse)
 		.def_readwrite("OwnerSteamID", &ValidateAuthTicketResponse_t::m_OwnerSteamID)
 	;
 	py::class_<MicroTxnAuthorizationResponse_t> _microtxnauthorizationresponse_t(m, "MicroTxnAuthorizationResponse");
 	_microtxnauthorizationresponse_t
 		.def_property_readonly("ptr", [](MicroTxnAuthorizationResponse_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("unAppID", &MicroTxnAuthorizationResponse_t::m_unAppID)
 		.def_readwrite("ulOrderID", &MicroTxnAuthorizationResponse_t::m_ulOrderID)
 		.def_readwrite("bAuthorized", &MicroTxnAuthorizationResponse_t::m_bAuthorized)
 	;
 	py::class_<EncryptedAppTicketResponse_t> _encryptedappticketresponse_t(m, "EncryptedAppTicketResponse");
 	_encryptedappticketresponse_t
 		.def_property_readonly("ptr", [](EncryptedAppTicketResponse_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("eResult", &EncryptedAppTicketResponse_t::m_eResult)
 	;
 	py::class_<GetAuthSessionTicketResponse_t> _getauthsessionticketresponse_t(m, "GetAuthSessionTicketResponse");
 	_getauthsessionticketresponse_t
 		.def_property_readonly("ptr", [](GetAuthSessionTicketResponse_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("hAuthTicket", &GetAuthSessionTicketResponse_t::m_hAuthTicket)
 		.def_readwrite("eResult", &GetAuthSessionTicketResponse_t::m_eResult)
 	;
 	py::class_<GameWebCallback_t> _gamewebcallback_t(m, "GameWebCallback");
 	_gamewebcallback_t
 		.def_property_readonly("ptr", [](GameWebCallback_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_property("szURL",
			[](const GameWebCallback_t& x) { return get_char_array(x.m_szURL); },
			[](GameWebCallback_t& x, const std::string& s) { set_char_array(x.m_szURL, s); })
 	;
 	py::class_<StoreAuthURLResponse_t> _storeauthurlresponse_t(m, "StoreAuthURLResponse");
 	_storeauthurlresponse_t
 		.def_property_readonly("ptr", [](StoreAuthURLResponse_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_property("szURL",
			[](const StoreAuthURLResponse_t& x) { return get_char_array(x.m_szURL); },
			[](StoreAuthURLResponse_t& x, const std::string& s) { set_char_array(x.m_szURL, s); })
 	;
 	py::class_<MarketEligibilityResponse_t> _marketeligibilityresponse_t(m, "MarketEligibilityResponse");
 	_marketeligibilityresponse_t
 		.def_property_readonly("ptr", [](MarketEligibilityResponse_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("bAllowed", &MarketEligibilityResponse_t::m_bAllowed)
 		.def_readwrite("eNotAllowedReason", &MarketEligibilityResponse_t::m_eNotAllowedReason)
 		.def_readwrite("rtAllowedAtTime", &MarketEligibilityResponse_t::m_rtAllowedAtTime)
 		.def_readwrite("cdaySteamGuardRequiredDays", &MarketEligibilityResponse_t::m_cdaySteamGuardRequiredDays)
 		.def_readwrite("cdayNewDeviceCooldown", &MarketEligibilityResponse_t::m_cdayNewDeviceCooldown)
 	;
 	py::class_<DurationControl_t> _durationcontrol_t(m, "DurationControl");
 	_durationcontrol_t
 		.def_property_readonly("ptr", [](DurationControl_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("eResult", &DurationControl_t::m_eResult)
 		.def_readwrite("appid", &DurationControl_t::m_appid)
 		.def_readwrite("bApplicable", &DurationControl_t::m_bApplicable)
 		.def_readwrite("csecsLast5h", &DurationControl_t::m_csecsLast5h)
 		.def_readwrite("progress", &DurationControl_t::m_progress)
 		.def_readwrite("notification", &DurationControl_t::m_notification)
 		.def_readwrite("csecsToday", &DurationControl_t::m_csecsToday)
 		.def_readwrite("csecsRemaining", &DurationControl_t::m_csecsRemaining)
 	;
 	py::class_<GetTicketForWebApiResponse_t> _getticketforwebapiresponse_t(m, "GetTicketForWebApiResponse");
 	_getticketforwebapiresponse_t
 		.def_property_readonly("ptr", [](GetTicketForWebApiResponse_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("hAuthTicket", &GetTicketForWebApiResponse_t::m_hAuthTicket)
 		.def_readwrite("eResult", &GetTicketForWebApiResponse_t::m_eResult)
 		.def_readwrite("cubTicket", &GetTicketForWebApiResponse_t::m_cubTicket)
 		.def_property_readonly("rgubTicket",
			[](GetTicketForWebApiResponse_t& x) { return FixedArrayView<uint8, 2560>{ x.m_rgubTicket }; },
			py::return_value_policy::reference_internal)
 	;
 	py::class_<PersonaStateChange_t> _personastatechange_t(m, "PersonaStateChange");
 	_personastatechange_t
 		.def_property_readonly("ptr", [](PersonaStateChange_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("ulSteamID", &PersonaStateChange_t::m_ulSteamID)
 		.def_readwrite("nChangeFlags", &PersonaStateChange_t::m_nChangeFlags)
 	;
 	py::class_<GameOverlayActivated_t> _gameoverlayactivated_t(m, "GameOverlayActivated");
 	_gameoverlayactivated_t
 		.def_property_readonly("ptr", [](GameOverlayActivated_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("bActive", &GameOverlayActivated_t::m_bActive)
 		.def_readwrite("bUserInitiated", &GameOverlayActivated_t::m_bUserInitiated)
 		.def_readwrite("nAppID", &GameOverlayActivated_t::m_nAppID)
 		.def_readwrite("dwOverlayPID", &GameOverlayActivated_t::m_dwOverlayPID)
 	;
 	py::class_<GameServerChangeRequested_t> _gameserverchangerequested_t(m, "GameServerChangeRequested");
 	_gameserverchangerequested_t
 		.def_property_readonly("ptr", [](GameServerChangeRequested_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_property("rgchServer",
			[](const GameServerChangeRequested_t& x) { return get_char_array(x.m_rgchServer); },
			[](GameServerChangeRequested_t& x, const std::string& s) { set_char_array(x.m_rgchServer, s); })
 		.def_property("rgchPassword",
			[](const GameServerChangeRequested_t& x) { return get_char_array(x.m_rgchPassword); },
			[](GameServerChangeRequested_t& x, const std::string& s) { set_char_array(x.m_rgchPassword, s); })
 	;
 	py::class_<GameLobbyJoinRequested_t> _gamelobbyjoinrequested_t(m, "GameLobbyJoinRequested");
 	_gamelobbyjoinrequested_t
 		.def_property_readonly("ptr", [](GameLobbyJoinRequested_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("steamIDLobby", &GameLobbyJoinRequested_t::m_steamIDLobby)
 		.def_readwrite("steamIDFriend", &GameLobbyJoinRequested_t::m_steamIDFriend)
 	;
 	py::class_<AvatarImageLoaded_t> _avatarimageloaded_t(m, "AvatarImageLoaded");
 	_avatarimageloaded_t
 		.def_property_readonly("ptr", [](AvatarImageLoaded_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("steamID", &AvatarImageLoaded_t::m_steamID)
 		.def_readwrite("iImage", &AvatarImageLoaded_t::m_iImage)
 		.def_readwrite("iWide", &AvatarImageLoaded_t::m_iWide)
 		.def_readwrite("iTall", &AvatarImageLoaded_t::m_iTall)
 	;
 	py::class_<ClanOfficerListResponse_t> _clanofficerlistresponse_t(m, "ClanOfficerListResponse");
 	_clanofficerlistresponse_t
 		.def_property_readonly("ptr", [](ClanOfficerListResponse_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("steamIDClan", &ClanOfficerListResponse_t::m_steamIDClan)
 		.def_readwrite("cOfficers", &ClanOfficerListResponse_t::m_cOfficers)
 		.def_readwrite("bSuccess", &ClanOfficerListResponse_t::m_bSuccess)
 	;
 	py::class_<FriendRichPresenceUpdate_t> _friendrichpresenceupdate_t(m, "FriendRichPresenceUpdate");
 	_friendrichpresenceupdate_t
 		.def_property_readonly("ptr", [](FriendRichPresenceUpdate_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("steamIDFriend", &FriendRichPresenceUpdate_t::m_steamIDFriend)
 		.def_readwrite("nAppID", &FriendRichPresenceUpdate_t::m_nAppID)
 	;
 	py::class_<GameRichPresenceJoinRequested_t> _gamerichpresencejoinrequested_t(m, "GameRichPresenceJoinRequested");
 	_gamerichpresencejoinrequested_t
 		.def_property_readonly("ptr", [](GameRichPresenceJoinRequested_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("steamIDFriend", &GameRichPresenceJoinRequested_t::m_steamIDFriend)
 		.def_property("rgchConnect",
			[](const GameRichPresenceJoinRequested_t& x) { return get_char_array(x.m_rgchConnect); },
			[](GameRichPresenceJoinRequested_t& x, const std::string& s) { set_char_array(x.m_rgchConnect, s); })
 	;
 	py::class_<GameConnectedClanChatMsg_t> _gameconnectedclanchatmsg_t(m, "GameConnectedClanChatMsg");
 	_gameconnectedclanchatmsg_t
 		.def_property_readonly("ptr", [](GameConnectedClanChatMsg_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("steamIDClanChat", &GameConnectedClanChatMsg_t::m_steamIDClanChat)
 		.def_readwrite("steamIDUser", &GameConnectedClanChatMsg_t::m_steamIDUser)
 		.def_readwrite("iMessageID", &GameConnectedClanChatMsg_t::m_iMessageID)
 	;
 	py::class_<GameConnectedChatJoin_t> _gameconnectedchatjoin_t(m, "GameConnectedChatJoin");
 	_gameconnectedchatjoin_t
 		.def_property_readonly("ptr", [](GameConnectedChatJoin_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("steamIDClanChat", &GameConnectedChatJoin_t::m_steamIDClanChat)
 		.def_readwrite("steamIDUser", &GameConnectedChatJoin_t::m_steamIDUser)
 	;
 	py::class_<GameConnectedChatLeave_t> _gameconnectedchatleave_t(m, "GameConnectedChatLeave");
 	_gameconnectedchatleave_t
 		.def_property_readonly("ptr", [](GameConnectedChatLeave_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("steamIDClanChat", &GameConnectedChatLeave_t::m_steamIDClanChat)
 		.def_readwrite("steamIDUser", &GameConnectedChatLeave_t::m_steamIDUser)
 		.def_readwrite("bKicked", &GameConnectedChatLeave_t::m_bKicked)
 		.def_readwrite("bDropped", &GameConnectedChatLeave_t::m_bDropped)
 	;
 	py::class_<DownloadClanActivityCountsResult_t> _downloadclanactivitycountsresult_t(m, "DownloadClanActivityCountsResult");
 	_downloadclanactivitycountsresult_t
 		.def_property_readonly("ptr", [](DownloadClanActivityCountsResult_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("bSuccess", &DownloadClanActivityCountsResult_t::m_bSuccess)
 	;
 	py::class_<JoinClanChatRoomCompletionResult_t> _joinclanchatroomcompletionresult_t(m, "JoinClanChatRoomCompletionResult");
 	_joinclanchatroomcompletionresult_t
 		.def_property_readonly("ptr", [](JoinClanChatRoomCompletionResult_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("steamIDClanChat", &JoinClanChatRoomCompletionResult_t::m_steamIDClanChat)
 		.def_readwrite("eChatRoomEnterResponse", &JoinClanChatRoomCompletionResult_t::m_eChatRoomEnterResponse)
 	;
 	py::class_<GameConnectedFriendChatMsg_t> _gameconnectedfriendchatmsg_t(m, "GameConnectedFriendChatMsg");
 	_gameconnectedfriendchatmsg_t
 		.def_property_readonly("ptr", [](GameConnectedFriendChatMsg_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("steamIDUser", &GameConnectedFriendChatMsg_t::m_steamIDUser)
 		.def_readwrite("iMessageID", &GameConnectedFriendChatMsg_t::m_iMessageID)
 	;
 	py::class_<FriendsGetFollowerCount_t> _friendsgetfollowercount_t(m, "FriendsGetFollowerCount");
 	_friendsgetfollowercount_t
 		.def_property_readonly("ptr", [](FriendsGetFollowerCount_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("eResult", &FriendsGetFollowerCount_t::m_eResult)
 		.def_readwrite("steamID", &FriendsGetFollowerCount_t::m_steamID)
 		.def_readwrite("nCount", &FriendsGetFollowerCount_t::m_nCount)
 	;
 	py::class_<FriendsIsFollowing_t> _friendsisfollowing_t(m, "FriendsIsFollowing");
 	_friendsisfollowing_t
 		.def_property_readonly("ptr", [](FriendsIsFollowing_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("eResult", &FriendsIsFollowing_t::m_eResult)
 		.def_readwrite("steamID", &FriendsIsFollowing_t::m_steamID)
 		.def_readwrite("bIsFollowing", &FriendsIsFollowing_t::m_bIsFollowing)
 	;
 	py::class_<FriendsEnumerateFollowingList_t> _friendsenumeratefollowinglist_t(m, "FriendsEnumerateFollowingList");
 	_friendsenumeratefollowinglist_t
 		.def_property_readonly("ptr", [](FriendsEnumerateFollowingList_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("eResult", &FriendsEnumerateFollowingList_t::m_eResult)
 		.def_property_readonly("rgSteamID",
			[](FriendsEnumerateFollowingList_t& x) { return FixedArrayView<CSteamID, 50>{ x.m_rgSteamID }; },
			py::return_value_policy::reference_internal)
 		.def_readwrite("nResultsReturned", &FriendsEnumerateFollowingList_t::m_nResultsReturned)
 		.def_readwrite("nTotalResultCount", &FriendsEnumerateFollowingList_t::m_nTotalResultCount)
 	;
 	py::class_<UnreadChatMessagesChanged_t> _unreadchatmessageschanged_t(m, "UnreadChatMessagesChanged");
 	_unreadchatmessageschanged_t
 		.def_property_readonly("ptr", [](UnreadChatMessagesChanged_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 	;
 	py::class_<OverlayBrowserProtocolNavigation_t> _overlaybrowserprotocolnavigation_t(m, "OverlayBrowserProtocolNavigation");
 	_overlaybrowserprotocolnavigation_t
 		.def_property_readonly("ptr", [](OverlayBrowserProtocolNavigation_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_property("rgchURI",
			[](const OverlayBrowserProtocolNavigation_t& x) { return get_char_array(x.rgchURI); },
			[](OverlayBrowserProtocolNavigation_t& x, const std::string& s) { set_char_array(x.rgchURI, s); })
 	;
 	py::class_<EquippedProfileItemsChanged_t> _equippedprofileitemschanged_t(m, "EquippedProfileItemsChanged");
 	_equippedprofileitemschanged_t
 		.def_property_readonly("ptr", [](EquippedProfileItemsChanged_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("steamID", &EquippedProfileItemsChanged_t::m_steamID)
 	;
 	py::class_<EquippedProfileItems_t> _equippedprofileitems_t(m, "EquippedProfileItems");
 	_equippedprofileitems_t
 		.def_property_readonly("ptr", [](EquippedProfileItems_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("eResult", &EquippedProfileItems_t::m_eResult)
 		.def_readwrite("steamID", &EquippedProfileItems_t::m_steamID)
 		.def_readwrite("bHasAnimatedAvatar", &EquippedProfileItems_t::m_bHasAnimatedAvatar)
 		.def_readwrite("bHasAvatarFrame", &EquippedProfileItems_t::m_bHasAvatarFrame)
 		.def_readwrite("bHasProfileModifier", &EquippedProfileItems_t::m_bHasProfileModifier)
 		.def_readwrite("bHasProfileBackground", &EquippedProfileItems_t::m_bHasProfileBackground)
 		.def_readwrite("bHasMiniProfileBackground", &EquippedProfileItems_t::m_bHasMiniProfileBackground)
 		.def_readwrite("bFromCache", &EquippedProfileItems_t::m_bFromCache)
 	;
 	py::class_<IPCountry_t> _ipcountry_t(m, "IPCountry");
 	_ipcountry_t
 		.def_property_readonly("ptr", [](IPCountry_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 	;
 	py::class_<LowBatteryPower_t> _lowbatterypower_t(m, "LowBatteryPower");
 	_lowbatterypower_t
 		.def_property_readonly("ptr", [](LowBatteryPower_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("nMinutesBatteryLeft", &LowBatteryPower_t::m_nMinutesBatteryLeft)
 	;
 	py::class_<SteamAPICallCompleted_t> _steamapicallcompleted_t(m, "SteamAPICallCompleted");
 	_steamapicallcompleted_t
 		.def_property_readonly("ptr", [](SteamAPICallCompleted_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("hAsyncCall", &SteamAPICallCompleted_t::m_hAsyncCall)
 		.def_readwrite("iCallback", &SteamAPICallCompleted_t::m_iCallback)
 		.def_readwrite("cubParam", &SteamAPICallCompleted_t::m_cubParam)
 	;
 	py::class_<SteamShutdown_t> _steamshutdown_t(m, "SteamShutdown");
 	_steamshutdown_t
 		.def_property_readonly("ptr", [](SteamShutdown_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 	;
 	py::class_<CheckFileSignature_t> _checkfilesignature_t(m, "CheckFileSignature");
 	_checkfilesignature_t
 		.def_property_readonly("ptr", [](CheckFileSignature_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("eCheckFileSignature", &CheckFileSignature_t::m_eCheckFileSignature)
 	;
 	py::class_<GamepadTextInputDismissed_t> _gamepadtextinputdismissed_t(m, "GamepadTextInputDismissed");
 	_gamepadtextinputdismissed_t
 		.def_property_readonly("ptr", [](GamepadTextInputDismissed_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("bSubmitted", &GamepadTextInputDismissed_t::m_bSubmitted)
 		.def_readwrite("unSubmittedText", &GamepadTextInputDismissed_t::m_unSubmittedText)
 		.def_readwrite("unAppID", &GamepadTextInputDismissed_t::m_unAppID)
 	;
 	py::class_<AppResumingFromSuspend_t> _appresumingfromsuspend_t(m, "AppResumingFromSuspend");
 	_appresumingfromsuspend_t
 		.def_property_readonly("ptr", [](AppResumingFromSuspend_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 	;
 	py::class_<FloatingGamepadTextInputDismissed_t> _floatinggamepadtextinputdismissed_t(m, "FloatingGamepadTextInputDismissed");
 	_floatinggamepadtextinputdismissed_t
 		.def_property_readonly("ptr", [](FloatingGamepadTextInputDismissed_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 	;
 	py::class_<FilterTextDictionaryChanged_t> _filtertextdictionarychanged_t(m, "FilterTextDictionaryChanged");
 	_filtertextdictionarychanged_t
 		.def_property_readonly("ptr", [](FilterTextDictionaryChanged_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("eLanguage", &FilterTextDictionaryChanged_t::m_eLanguage)
 	;
 	py::class_<FavoritesListChanged_t> _favoriteslistchanged_t(m, "FavoritesListChanged");
 	_favoriteslistchanged_t
 		.def_property_readonly("ptr", [](FavoritesListChanged_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("nIP", &FavoritesListChanged_t::m_nIP)
 		.def_readwrite("nQueryPort", &FavoritesListChanged_t::m_nQueryPort)
 		.def_readwrite("nConnPort", &FavoritesListChanged_t::m_nConnPort)
 		.def_readwrite("nAppID", &FavoritesListChanged_t::m_nAppID)
 		.def_readwrite("nFlags", &FavoritesListChanged_t::m_nFlags)
 		.def_readwrite("bAdd", &FavoritesListChanged_t::m_bAdd)
 		.def_readwrite("unAccountId", &FavoritesListChanged_t::m_unAccountId)
 	;
 	py::class_<LobbyInvite_t> _lobbyinvite_t(m, "LobbyInvite");
 	_lobbyinvite_t
 		.def_property_readonly("ptr", [](LobbyInvite_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("ulSteamIDUser", &LobbyInvite_t::m_ulSteamIDUser)
 		.def_readwrite("ulSteamIDLobby", &LobbyInvite_t::m_ulSteamIDLobby)
 		.def_readwrite("ulGameID", &LobbyInvite_t::m_ulGameID)
 	;
 	py::class_<LobbyEnter_t> _lobbyenter_t(m, "LobbyEnter");
 	_lobbyenter_t
 		.def_property_readonly("ptr", [](LobbyEnter_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("ulSteamIDLobby", &LobbyEnter_t::m_ulSteamIDLobby)
 		.def_readwrite("rgfChatPermissions", &LobbyEnter_t::m_rgfChatPermissions)
 		.def_readwrite("bLocked", &LobbyEnter_t::m_bLocked)
 		.def_readwrite("EChatRoomEnterResponse", &LobbyEnter_t::m_EChatRoomEnterResponse)
 	;
 	py::class_<LobbyDataUpdate_t> _lobbydataupdate_t(m, "LobbyDataUpdate");
 	_lobbydataupdate_t
 		.def_property_readonly("ptr", [](LobbyDataUpdate_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("ulSteamIDLobby", &LobbyDataUpdate_t::m_ulSteamIDLobby)
 		.def_readwrite("ulSteamIDMember", &LobbyDataUpdate_t::m_ulSteamIDMember)
 		.def_readwrite("bSuccess", &LobbyDataUpdate_t::m_bSuccess)
 	;
 	py::class_<LobbyChatUpdate_t> _lobbychatupdate_t(m, "LobbyChatUpdate");
 	_lobbychatupdate_t
 		.def_property_readonly("ptr", [](LobbyChatUpdate_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("ulSteamIDLobby", &LobbyChatUpdate_t::m_ulSteamIDLobby)
 		.def_readwrite("ulSteamIDUserChanged", &LobbyChatUpdate_t::m_ulSteamIDUserChanged)
 		.def_readwrite("ulSteamIDMakingChange", &LobbyChatUpdate_t::m_ulSteamIDMakingChange)
 		.def_readwrite("rgfChatMemberStateChange", &LobbyChatUpdate_t::m_rgfChatMemberStateChange)
 	;
 	py::class_<LobbyChatMsg_t> _lobbychatmsg_t(m, "LobbyChatMsg");
 	_lobbychatmsg_t
 		.def_property_readonly("ptr", [](LobbyChatMsg_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("ulSteamIDLobby", &LobbyChatMsg_t::m_ulSteamIDLobby)
 		.def_readwrite("ulSteamIDUser", &LobbyChatMsg_t::m_ulSteamIDUser)
 		.def_readwrite("eChatEntryType", &LobbyChatMsg_t::m_eChatEntryType)
 		.def_readwrite("iChatID", &LobbyChatMsg_t::m_iChatID)
 	;
 	py::class_<LobbyGameCreated_t> _lobbygamecreated_t(m, "LobbyGameCreated");
 	_lobbygamecreated_t
 		.def_property_readonly("ptr", [](LobbyGameCreated_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("ulSteamIDLobby", &LobbyGameCreated_t::m_ulSteamIDLobby)
 		.def_readwrite("ulSteamIDGameServer", &LobbyGameCreated_t::m_ulSteamIDGameServer)
 		.def_readwrite("unIP", &LobbyGameCreated_t::m_unIP)
 		.def_readwrite("usPort", &LobbyGameCreated_t::m_usPort)
 	;
 	py::class_<LobbyMatchList_t> _lobbymatchlist_t(m, "LobbyMatchList");
 	_lobbymatchlist_t
 		.def_property_readonly("ptr", [](LobbyMatchList_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("nLobbiesMatching", &LobbyMatchList_t::m_nLobbiesMatching)
 	;
 	py::class_<LobbyKicked_t> _lobbykicked_t(m, "LobbyKicked");
 	_lobbykicked_t
 		.def_property_readonly("ptr", [](LobbyKicked_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("ulSteamIDLobby", &LobbyKicked_t::m_ulSteamIDLobby)
 		.def_readwrite("ulSteamIDAdmin", &LobbyKicked_t::m_ulSteamIDAdmin)
 		.def_readwrite("bKickedDueToDisconnect", &LobbyKicked_t::m_bKickedDueToDisconnect)
 	;
 	py::class_<LobbyCreated_t> _lobbycreated_t(m, "LobbyCreated");
 	_lobbycreated_t
 		.def_property_readonly("ptr", [](LobbyCreated_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("eResult", &LobbyCreated_t::m_eResult)
 		.def_readwrite("ulSteamIDLobby", &LobbyCreated_t::m_ulSteamIDLobby)
 	;
 	py::class_<FavoritesListAccountsUpdated_t> _favoriteslistaccountsupdated_t(m, "FavoritesListAccountsUpdated");
 	_favoriteslistaccountsupdated_t
 		.def_property_readonly("ptr", [](FavoritesListAccountsUpdated_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("eResult", &FavoritesListAccountsUpdated_t::m_eResult)
 	;
 	py::class_<JoinPartyCallback_t> _joinpartycallback_t(m, "JoinPartyCallback");
 	_joinpartycallback_t
 		.def_property_readonly("ptr", [](JoinPartyCallback_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("eResult", &JoinPartyCallback_t::m_eResult)
 		.def_readwrite("ulBeaconID", &JoinPartyCallback_t::m_ulBeaconID)
 		.def_readwrite("SteamIDBeaconOwner", &JoinPartyCallback_t::m_SteamIDBeaconOwner)
 		.def_property("rgchConnectString",
			[](const JoinPartyCallback_t& x) { return get_char_array(x.m_rgchConnectString); },
			[](JoinPartyCallback_t& x, const std::string& s) { set_char_array(x.m_rgchConnectString, s); })
 	;
 	py::class_<CreateBeaconCallback_t> _createbeaconcallback_t(m, "CreateBeaconCallback");
 	_createbeaconcallback_t
 		.def_property_readonly("ptr", [](CreateBeaconCallback_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("eResult", &CreateBeaconCallback_t::m_eResult)
 		.def_readwrite("ulBeaconID", &CreateBeaconCallback_t::m_ulBeaconID)
 	;
 	py::class_<ReservationNotificationCallback_t> _reservationnotificationcallback_t(m, "ReservationNotificationCallback");
 	_reservationnotificationcallback_t
 		.def_property_readonly("ptr", [](ReservationNotificationCallback_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("ulBeaconID", &ReservationNotificationCallback_t::m_ulBeaconID)
 		.def_readwrite("steamIDJoiner", &ReservationNotificationCallback_t::m_steamIDJoiner)
 	;
 	py::class_<ChangeNumOpenSlotsCallback_t> _changenumopenslotscallback_t(m, "ChangeNumOpenSlotsCallback");
 	_changenumopenslotscallback_t
 		.def_property_readonly("ptr", [](ChangeNumOpenSlotsCallback_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("eResult", &ChangeNumOpenSlotsCallback_t::m_eResult)
 	;
 	py::class_<AvailableBeaconLocationsUpdated_t> _availablebeaconlocationsupdated_t(m, "AvailableBeaconLocationsUpdated");
 	_availablebeaconlocationsupdated_t
 		.def_property_readonly("ptr", [](AvailableBeaconLocationsUpdated_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 	;
 	py::class_<ActiveBeaconsUpdated_t> _activebeaconsupdated_t(m, "ActiveBeaconsUpdated");
 	_activebeaconsupdated_t
 		.def_property_readonly("ptr", [](ActiveBeaconsUpdated_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 	;
 	py::class_<RemoteStorageFileShareResult_t> _remotestoragefileshareresult_t(m, "RemoteStorageFileShareResult");
 	_remotestoragefileshareresult_t
 		.def_property_readonly("ptr", [](RemoteStorageFileShareResult_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("eResult", &RemoteStorageFileShareResult_t::m_eResult)
 		.def_readwrite("hFile", &RemoteStorageFileShareResult_t::m_hFile)
 		.def_property("rgchFilename",
			[](const RemoteStorageFileShareResult_t& x) { return get_char_array(x.m_rgchFilename); },
			[](RemoteStorageFileShareResult_t& x, const std::string& s) { set_char_array(x.m_rgchFilename, s); })
 	;
 	py::class_<RemoteStoragePublishFileResult_t> _remotestoragepublishfileresult_t(m, "RemoteStoragePublishFileResult");
 	_remotestoragepublishfileresult_t
 		.def_property_readonly("ptr", [](RemoteStoragePublishFileResult_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("eResult", &RemoteStoragePublishFileResult_t::m_eResult)
 		.def_readwrite("nPublishedFileId", &RemoteStoragePublishFileResult_t::m_nPublishedFileId)
 		.def_readwrite("bUserNeedsToAcceptWorkshopLegalAgreement", &RemoteStoragePublishFileResult_t::m_bUserNeedsToAcceptWorkshopLegalAgreement)
 	;
 	py::class_<RemoteStorageDeletePublishedFileResult_t> _remotestoragedeletepublishedfileresult_t(m, "RemoteStorageDeletePublishedFileResult");
 	_remotestoragedeletepublishedfileresult_t
 		.def_property_readonly("ptr", [](RemoteStorageDeletePublishedFileResult_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("eResult", &RemoteStorageDeletePublishedFileResult_t::m_eResult)
 		.def_readwrite("nPublishedFileId", &RemoteStorageDeletePublishedFileResult_t::m_nPublishedFileId)
 	;
 	py::class_<RemoteStorageEnumerateUserPublishedFilesResult_t> _remotestorageenumerateuserpublishedfilesresult_t(m, "RemoteStorageEnumerateUserPublishedFilesResult");
 	_remotestorageenumerateuserpublishedfilesresult_t
 		.def_property_readonly("ptr", [](RemoteStorageEnumerateUserPublishedFilesResult_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("eResult", &RemoteStorageEnumerateUserPublishedFilesResult_t::m_eResult)
 		.def_readwrite("nResultsReturned", &RemoteStorageEnumerateUserPublishedFilesResult_t::m_nResultsReturned)
 		.def_readwrite("nTotalResultCount", &RemoteStorageEnumerateUserPublishedFilesResult_t::m_nTotalResultCount)
 		.def_property_readonly("rgPublishedFileId",
			[](RemoteStorageEnumerateUserPublishedFilesResult_t& x) { return FixedArrayView<PublishedFileId_t, 50>{ x.m_rgPublishedFileId }; },
			py::return_value_policy::reference_internal)
 	;
 	py::class_<RemoteStorageSubscribePublishedFileResult_t> _remotestoragesubscribepublishedfileresult_t(m, "RemoteStorageSubscribePublishedFileResult");
 	_remotestoragesubscribepublishedfileresult_t
 		.def_property_readonly("ptr", [](RemoteStorageSubscribePublishedFileResult_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("eResult", &RemoteStorageSubscribePublishedFileResult_t::m_eResult)
 		.def_readwrite("nPublishedFileId", &RemoteStorageSubscribePublishedFileResult_t::m_nPublishedFileId)
 	;
 	py::class_<RemoteStorageEnumerateUserSubscribedFilesResult_t> _remotestorageenumerateusersubscribedfilesresult_t(m, "RemoteStorageEnumerateUserSubscribedFilesResult");
 	_remotestorageenumerateusersubscribedfilesresult_t
 		.def_property_readonly("ptr", [](RemoteStorageEnumerateUserSubscribedFilesResult_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("eResult", &RemoteStorageEnumerateUserSubscribedFilesResult_t::m_eResult)
 		.def_readwrite("nResultsReturned", &RemoteStorageEnumerateUserSubscribedFilesResult_t::m_nResultsReturned)
 		.def_readwrite("nTotalResultCount", &RemoteStorageEnumerateUserSubscribedFilesResult_t::m_nTotalResultCount)
 		.def_property_readonly("rgPublishedFileId",
			[](RemoteStorageEnumerateUserSubscribedFilesResult_t& x) { return FixedArrayView<PublishedFileId_t, 50>{ x.m_rgPublishedFileId }; },
			py::return_value_policy::reference_internal)
 		.def_property_readonly("rgRTimeSubscribed",
			[](RemoteStorageEnumerateUserSubscribedFilesResult_t& x) { return FixedArrayView<uint32, 50>{ x.m_rgRTimeSubscribed }; },
			py::return_value_policy::reference_internal)
 	;
 	py::class_<RemoteStorageUnsubscribePublishedFileResult_t> _remotestorageunsubscribepublishedfileresult_t(m, "RemoteStorageUnsubscribePublishedFileResult");
 	_remotestorageunsubscribepublishedfileresult_t
 		.def_property_readonly("ptr", [](RemoteStorageUnsubscribePublishedFileResult_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("eResult", &RemoteStorageUnsubscribePublishedFileResult_t::m_eResult)
 		.def_readwrite("nPublishedFileId", &RemoteStorageUnsubscribePublishedFileResult_t::m_nPublishedFileId)
 	;
 	py::class_<RemoteStorageUpdatePublishedFileResult_t> _remotestorageupdatepublishedfileresult_t(m, "RemoteStorageUpdatePublishedFileResult");
 	_remotestorageupdatepublishedfileresult_t
 		.def_property_readonly("ptr", [](RemoteStorageUpdatePublishedFileResult_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("eResult", &RemoteStorageUpdatePublishedFileResult_t::m_eResult)
 		.def_readwrite("nPublishedFileId", &RemoteStorageUpdatePublishedFileResult_t::m_nPublishedFileId)
 		.def_readwrite("bUserNeedsToAcceptWorkshopLegalAgreement", &RemoteStorageUpdatePublishedFileResult_t::m_bUserNeedsToAcceptWorkshopLegalAgreement)
 	;
 	py::class_<RemoteStorageDownloadUGCResult_t> _remotestoragedownloadugcresult_t(m, "RemoteStorageDownloadUGCResult");
 	_remotestoragedownloadugcresult_t
 		.def_property_readonly("ptr", [](RemoteStorageDownloadUGCResult_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("eResult", &RemoteStorageDownloadUGCResult_t::m_eResult)
 		.def_readwrite("hFile", &RemoteStorageDownloadUGCResult_t::m_hFile)
 		.def_readwrite("nAppID", &RemoteStorageDownloadUGCResult_t::m_nAppID)
 		.def_readwrite("nSizeInBytes", &RemoteStorageDownloadUGCResult_t::m_nSizeInBytes)
 		.def_property("pchFileName",
			[](const RemoteStorageDownloadUGCResult_t& x) { return get_char_array(x.m_pchFileName); },
			[](RemoteStorageDownloadUGCResult_t& x, const std::string& s) { set_char_array(x.m_pchFileName, s); })
 		.def_readwrite("ulSteamIDOwner", &RemoteStorageDownloadUGCResult_t::m_ulSteamIDOwner)
 	;
 	py::class_<RemoteStorageGetPublishedFileDetailsResult_t> _remotestoragegetpublishedfiledetailsresult_t(m, "RemoteStorageGetPublishedFileDetailsResult");
 	_remotestoragegetpublishedfiledetailsresult_t
 		.def_property_readonly("ptr", [](RemoteStorageGetPublishedFileDetailsResult_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("eResult", &RemoteStorageGetPublishedFileDetailsResult_t::m_eResult)
 		.def_readwrite("nPublishedFileId", &RemoteStorageGetPublishedFileDetailsResult_t::m_nPublishedFileId)
 		.def_readwrite("nCreatorAppID", &RemoteStorageGetPublishedFileDetailsResult_t::m_nCreatorAppID)
 		.def_readwrite("nConsumerAppID", &RemoteStorageGetPublishedFileDetailsResult_t::m_nConsumerAppID)
 		.def_property("rgchTitle",
			[](const RemoteStorageGetPublishedFileDetailsResult_t& x) { return get_char_array(x.m_rgchTitle); },
			[](RemoteStorageGetPublishedFileDetailsResult_t& x, const std::string& s) { set_char_array(x.m_rgchTitle, s); })
 		.def_property("rgchDescription",
			[](const RemoteStorageGetPublishedFileDetailsResult_t& x) { return get_char_array(x.m_rgchDescription); },
			[](RemoteStorageGetPublishedFileDetailsResult_t& x, const std::string& s) { set_char_array(x.m_rgchDescription, s); })
 		.def_readwrite("hFile", &RemoteStorageGetPublishedFileDetailsResult_t::m_hFile)
 		.def_readwrite("hPreviewFile", &RemoteStorageGetPublishedFileDetailsResult_t::m_hPreviewFile)
 		.def_readwrite("ulSteamIDOwner", &RemoteStorageGetPublishedFileDetailsResult_t::m_ulSteamIDOwner)
 		.def_readwrite("rtimeCreated", &RemoteStorageGetPublishedFileDetailsResult_t::m_rtimeCreated)
 		.def_readwrite("rtimeUpdated", &RemoteStorageGetPublishedFileDetailsResult_t::m_rtimeUpdated)
 		.def_readwrite("eVisibility", &RemoteStorageGetPublishedFileDetailsResult_t::m_eVisibility)
 		.def_readwrite("bBanned", &RemoteStorageGetPublishedFileDetailsResult_t::m_bBanned)
 		.def_property("rgchTags",
			[](const RemoteStorageGetPublishedFileDetailsResult_t& x) { return get_char_array(x.m_rgchTags); },
			[](RemoteStorageGetPublishedFileDetailsResult_t& x, const std::string& s) { set_char_array(x.m_rgchTags, s); })
 		.def_readwrite("bTagsTruncated", &RemoteStorageGetPublishedFileDetailsResult_t::m_bTagsTruncated)
 		.def_property("pchFileName",
			[](const RemoteStorageGetPublishedFileDetailsResult_t& x) { return get_char_array(x.m_pchFileName); },
			[](RemoteStorageGetPublishedFileDetailsResult_t& x, const std::string& s) { set_char_array(x.m_pchFileName, s); })
 		.def_readwrite("nFileSize", &RemoteStorageGetPublishedFileDetailsResult_t::m_nFileSize)
 		.def_readwrite("nPreviewFileSize", &RemoteStorageGetPublishedFileDetailsResult_t::m_nPreviewFileSize)
 		.def_property("rgchURL",
			[](const RemoteStorageGetPublishedFileDetailsResult_t& x) { return get_char_array(x.m_rgchURL); },
			[](RemoteStorageGetPublishedFileDetailsResult_t& x, const std::string& s) { set_char_array(x.m_rgchURL, s); })
 		.def_readwrite("eFileType", &RemoteStorageGetPublishedFileDetailsResult_t::m_eFileType)
 		.def_readwrite("bAcceptedForUse", &RemoteStorageGetPublishedFileDetailsResult_t::m_bAcceptedForUse)
 	;
 	py::class_<RemoteStorageEnumerateWorkshopFilesResult_t> _remotestorageenumerateworkshopfilesresult_t(m, "RemoteStorageEnumerateWorkshopFilesResult");
 	_remotestorageenumerateworkshopfilesresult_t
 		.def_property_readonly("ptr", [](RemoteStorageEnumerateWorkshopFilesResult_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("eResult", &RemoteStorageEnumerateWorkshopFilesResult_t::m_eResult)
 		.def_readwrite("nResultsReturned", &RemoteStorageEnumerateWorkshopFilesResult_t::m_nResultsReturned)
 		.def_readwrite("nTotalResultCount", &RemoteStorageEnumerateWorkshopFilesResult_t::m_nTotalResultCount)
 		.def_property_readonly("rgPublishedFileId",
			[](RemoteStorageEnumerateWorkshopFilesResult_t& x) { return FixedArrayView<PublishedFileId_t, 50>{ x.m_rgPublishedFileId }; },
			py::return_value_policy::reference_internal)
 		.def_property_readonly("rgScore",
			[](RemoteStorageEnumerateWorkshopFilesResult_t& x) { return FixedArrayView<float, 50>{ x.m_rgScore }; },
			py::return_value_policy::reference_internal)
 		.def_readwrite("nAppId", &RemoteStorageEnumerateWorkshopFilesResult_t::m_nAppId)
 		.def_readwrite("unStartIndex", &RemoteStorageEnumerateWorkshopFilesResult_t::m_unStartIndex)
 	;
 	py::class_<RemoteStorageGetPublishedItemVoteDetailsResult_t> _remotestoragegetpublisheditemvotedetailsresult_t(m, "RemoteStorageGetPublishedItemVoteDetailsResult");
 	_remotestoragegetpublisheditemvotedetailsresult_t
 		.def_property_readonly("ptr", [](RemoteStorageGetPublishedItemVoteDetailsResult_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("eResult", &RemoteStorageGetPublishedItemVoteDetailsResult_t::m_eResult)
 		.def_readwrite("unPublishedFileId", &RemoteStorageGetPublishedItemVoteDetailsResult_t::m_unPublishedFileId)
 		.def_readwrite("nVotesFor", &RemoteStorageGetPublishedItemVoteDetailsResult_t::m_nVotesFor)
 		.def_readwrite("nVotesAgainst", &RemoteStorageGetPublishedItemVoteDetailsResult_t::m_nVotesAgainst)
 		.def_readwrite("nReports", &RemoteStorageGetPublishedItemVoteDetailsResult_t::m_nReports)
 		.def_readwrite("fScore", &RemoteStorageGetPublishedItemVoteDetailsResult_t::m_fScore)
 	;
 	py::class_<RemoteStoragePublishedFileSubscribed_t> _remotestoragepublishedfilesubscribed_t(m, "RemoteStoragePublishedFileSubscribed");
 	_remotestoragepublishedfilesubscribed_t
 		.def_property_readonly("ptr", [](RemoteStoragePublishedFileSubscribed_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("nPublishedFileId", &RemoteStoragePublishedFileSubscribed_t::m_nPublishedFileId)
 		.def_readwrite("nAppID", &RemoteStoragePublishedFileSubscribed_t::m_nAppID)
 	;
 	py::class_<RemoteStoragePublishedFileUnsubscribed_t> _remotestoragepublishedfileunsubscribed_t(m, "RemoteStoragePublishedFileUnsubscribed");
 	_remotestoragepublishedfileunsubscribed_t
 		.def_property_readonly("ptr", [](RemoteStoragePublishedFileUnsubscribed_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("nPublishedFileId", &RemoteStoragePublishedFileUnsubscribed_t::m_nPublishedFileId)
 		.def_readwrite("nAppID", &RemoteStoragePublishedFileUnsubscribed_t::m_nAppID)
 	;
 	py::class_<RemoteStoragePublishedFileDeleted_t> _remotestoragepublishedfiledeleted_t(m, "RemoteStoragePublishedFileDeleted");
 	_remotestoragepublishedfiledeleted_t
 		.def_property_readonly("ptr", [](RemoteStoragePublishedFileDeleted_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("nPublishedFileId", &RemoteStoragePublishedFileDeleted_t::m_nPublishedFileId)
 		.def_readwrite("nAppID", &RemoteStoragePublishedFileDeleted_t::m_nAppID)
 	;
 	py::class_<RemoteStorageUpdateUserPublishedItemVoteResult_t> _remotestorageupdateuserpublisheditemvoteresult_t(m, "RemoteStorageUpdateUserPublishedItemVoteResult");
 	_remotestorageupdateuserpublisheditemvoteresult_t
 		.def_property_readonly("ptr", [](RemoteStorageUpdateUserPublishedItemVoteResult_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("eResult", &RemoteStorageUpdateUserPublishedItemVoteResult_t::m_eResult)
 		.def_readwrite("nPublishedFileId", &RemoteStorageUpdateUserPublishedItemVoteResult_t::m_nPublishedFileId)
 	;
 	py::class_<RemoteStorageUserVoteDetails_t> _remotestorageuservotedetails_t(m, "RemoteStorageUserVoteDetails");
 	_remotestorageuservotedetails_t
 		.def_property_readonly("ptr", [](RemoteStorageUserVoteDetails_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("eResult", &RemoteStorageUserVoteDetails_t::m_eResult)
 		.def_readwrite("nPublishedFileId", &RemoteStorageUserVoteDetails_t::m_nPublishedFileId)
 		.def_readwrite("eVote", &RemoteStorageUserVoteDetails_t::m_eVote)
 	;
 	py::class_<RemoteStorageEnumerateUserSharedWorkshopFilesResult_t> _remotestorageenumerateusersharedworkshopfilesresult_t(m, "RemoteStorageEnumerateUserSharedWorkshopFilesResult");
 	_remotestorageenumerateusersharedworkshopfilesresult_t
 		.def_property_readonly("ptr", [](RemoteStorageEnumerateUserSharedWorkshopFilesResult_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("eResult", &RemoteStorageEnumerateUserSharedWorkshopFilesResult_t::m_eResult)
 		.def_readwrite("nResultsReturned", &RemoteStorageEnumerateUserSharedWorkshopFilesResult_t::m_nResultsReturned)
 		.def_readwrite("nTotalResultCount", &RemoteStorageEnumerateUserSharedWorkshopFilesResult_t::m_nTotalResultCount)
 		.def_property_readonly("rgPublishedFileId",
			[](RemoteStorageEnumerateUserSharedWorkshopFilesResult_t& x) { return FixedArrayView<PublishedFileId_t, 50>{ x.m_rgPublishedFileId }; },
			py::return_value_policy::reference_internal)
 	;
 	py::class_<RemoteStorageSetUserPublishedFileActionResult_t> _remotestoragesetuserpublishedfileactionresult_t(m, "RemoteStorageSetUserPublishedFileActionResult");
 	_remotestoragesetuserpublishedfileactionresult_t
 		.def_property_readonly("ptr", [](RemoteStorageSetUserPublishedFileActionResult_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("eResult", &RemoteStorageSetUserPublishedFileActionResult_t::m_eResult)
 		.def_readwrite("nPublishedFileId", &RemoteStorageSetUserPublishedFileActionResult_t::m_nPublishedFileId)
 		.def_readwrite("eAction", &RemoteStorageSetUserPublishedFileActionResult_t::m_eAction)
 	;
 	py::class_<RemoteStorageEnumeratePublishedFilesByUserActionResult_t> _remotestorageenumeratepublishedfilesbyuseractionresult_t(m, "RemoteStorageEnumeratePublishedFilesByUserActionResult");
 	_remotestorageenumeratepublishedfilesbyuseractionresult_t
 		.def_property_readonly("ptr", [](RemoteStorageEnumeratePublishedFilesByUserActionResult_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("eResult", &RemoteStorageEnumeratePublishedFilesByUserActionResult_t::m_eResult)
 		.def_readwrite("eAction", &RemoteStorageEnumeratePublishedFilesByUserActionResult_t::m_eAction)
 		.def_readwrite("nResultsReturned", &RemoteStorageEnumeratePublishedFilesByUserActionResult_t::m_nResultsReturned)
 		.def_readwrite("nTotalResultCount", &RemoteStorageEnumeratePublishedFilesByUserActionResult_t::m_nTotalResultCount)
 		.def_property_readonly("rgPublishedFileId",
			[](RemoteStorageEnumeratePublishedFilesByUserActionResult_t& x) { return FixedArrayView<PublishedFileId_t, 50>{ x.m_rgPublishedFileId }; },
			py::return_value_policy::reference_internal)
 		.def_property_readonly("rgRTimeUpdated",
			[](RemoteStorageEnumeratePublishedFilesByUserActionResult_t& x) { return FixedArrayView<uint32, 50>{ x.m_rgRTimeUpdated }; },
			py::return_value_policy::reference_internal)
 	;
 	py::class_<RemoteStoragePublishFileProgress_t> _remotestoragepublishfileprogress_t(m, "RemoteStoragePublishFileProgress");
 	_remotestoragepublishfileprogress_t
 		.def_property_readonly("ptr", [](RemoteStoragePublishFileProgress_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("dPercentFile", &RemoteStoragePublishFileProgress_t::m_dPercentFile)
 		.def_readwrite("bPreview", &RemoteStoragePublishFileProgress_t::m_bPreview)
 	;
 	py::class_<RemoteStoragePublishedFileUpdated_t> _remotestoragepublishedfileupdated_t(m, "RemoteStoragePublishedFileUpdated");
 	_remotestoragepublishedfileupdated_t
 		.def_property_readonly("ptr", [](RemoteStoragePublishedFileUpdated_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("nPublishedFileId", &RemoteStoragePublishedFileUpdated_t::m_nPublishedFileId)
 		.def_readwrite("nAppID", &RemoteStoragePublishedFileUpdated_t::m_nAppID)
 		.def_readwrite("ulUnused", &RemoteStoragePublishedFileUpdated_t::m_ulUnused)
 	;
 	py::class_<RemoteStorageFileWriteAsyncComplete_t> _remotestoragefilewriteasynccomplete_t(m, "RemoteStorageFileWriteAsyncComplete");
 	_remotestoragefilewriteasynccomplete_t
 		.def_property_readonly("ptr", [](RemoteStorageFileWriteAsyncComplete_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("eResult", &RemoteStorageFileWriteAsyncComplete_t::m_eResult)
 	;
 	py::class_<RemoteStorageFileReadAsyncComplete_t> _remotestoragefilereadasynccomplete_t(m, "RemoteStorageFileReadAsyncComplete");
 	_remotestoragefilereadasynccomplete_t
 		.def_property_readonly("ptr", [](RemoteStorageFileReadAsyncComplete_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("hFileReadAsync", &RemoteStorageFileReadAsyncComplete_t::m_hFileReadAsync)
 		.def_readwrite("eResult", &RemoteStorageFileReadAsyncComplete_t::m_eResult)
 		.def_readwrite("nOffset", &RemoteStorageFileReadAsyncComplete_t::m_nOffset)
 		.def_readwrite("cubRead", &RemoteStorageFileReadAsyncComplete_t::m_cubRead)
 	;
 	py::class_<RemoteStorageLocalFileChange_t> _remotestoragelocalfilechange_t(m, "RemoteStorageLocalFileChange");
 	_remotestoragelocalfilechange_t
 		.def_property_readonly("ptr", [](RemoteStorageLocalFileChange_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 	;
 	py::class_<UserStatsReceived_t> _userstatsreceived_t(m, "UserStatsReceived");
 	_userstatsreceived_t
 		.def_property_readonly("ptr", [](UserStatsReceived_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("nGameID", &UserStatsReceived_t::m_nGameID)
 		.def_readwrite("eResult", &UserStatsReceived_t::m_eResult)
 		.def_readwrite("steamIDUser", &UserStatsReceived_t::m_steamIDUser)
 	;
 	py::class_<UserStatsStored_t> _userstatsstored_t(m, "UserStatsStored");
 	_userstatsstored_t
 		.def_property_readonly("ptr", [](UserStatsStored_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("nGameID", &UserStatsStored_t::m_nGameID)
 		.def_readwrite("eResult", &UserStatsStored_t::m_eResult)
 	;
 	py::class_<UserAchievementStored_t> _userachievementstored_t(m, "UserAchievementStored");
 	_userachievementstored_t
 		.def_property_readonly("ptr", [](UserAchievementStored_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("nGameID", &UserAchievementStored_t::m_nGameID)
 		.def_readwrite("bGroupAchievement", &UserAchievementStored_t::m_bGroupAchievement)
 		.def_property("rgchAchievementName",
			[](const UserAchievementStored_t& x) { return get_char_array(x.m_rgchAchievementName); },
			[](UserAchievementStored_t& x, const std::string& s) { set_char_array(x.m_rgchAchievementName, s); })
 		.def_readwrite("nCurProgress", &UserAchievementStored_t::m_nCurProgress)
 		.def_readwrite("nMaxProgress", &UserAchievementStored_t::m_nMaxProgress)
 	;
 	py::class_<LeaderboardFindResult_t> _leaderboardfindresult_t(m, "LeaderboardFindResult");
 	_leaderboardfindresult_t
 		.def_property_readonly("ptr", [](LeaderboardFindResult_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("hSteamLeaderboard", &LeaderboardFindResult_t::m_hSteamLeaderboard)
 		.def_readwrite("bLeaderboardFound", &LeaderboardFindResult_t::m_bLeaderboardFound)
 	;
 	py::class_<LeaderboardScoresDownloaded_t> _leaderboardscoresdownloaded_t(m, "LeaderboardScoresDownloaded");
 	_leaderboardscoresdownloaded_t
 		.def_property_readonly("ptr", [](LeaderboardScoresDownloaded_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("hSteamLeaderboard", &LeaderboardScoresDownloaded_t::m_hSteamLeaderboard)
 		.def_readwrite("hSteamLeaderboardEntries", &LeaderboardScoresDownloaded_t::m_hSteamLeaderboardEntries)
 		.def_readwrite("cEntryCount", &LeaderboardScoresDownloaded_t::m_cEntryCount)
 	;
 	py::class_<LeaderboardScoreUploaded_t> _leaderboardscoreuploaded_t(m, "LeaderboardScoreUploaded");
 	_leaderboardscoreuploaded_t
 		.def_property_readonly("ptr", [](LeaderboardScoreUploaded_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("bSuccess", &LeaderboardScoreUploaded_t::m_bSuccess)
 		.def_readwrite("hSteamLeaderboard", &LeaderboardScoreUploaded_t::m_hSteamLeaderboard)
 		.def_readwrite("nScore", &LeaderboardScoreUploaded_t::m_nScore)
 		.def_readwrite("bScoreChanged", &LeaderboardScoreUploaded_t::m_bScoreChanged)
 		.def_readwrite("nGlobalRankNew", &LeaderboardScoreUploaded_t::m_nGlobalRankNew)
 		.def_readwrite("nGlobalRankPrevious", &LeaderboardScoreUploaded_t::m_nGlobalRankPrevious)
 	;
 	py::class_<NumberOfCurrentPlayers_t> _numberofcurrentplayers_t(m, "NumberOfCurrentPlayers");
 	_numberofcurrentplayers_t
 		.def_property_readonly("ptr", [](NumberOfCurrentPlayers_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("bSuccess", &NumberOfCurrentPlayers_t::m_bSuccess)
 		.def_readwrite("cPlayers", &NumberOfCurrentPlayers_t::m_cPlayers)
 	;
 	py::class_<UserStatsUnloaded_t> _userstatsunloaded_t(m, "UserStatsUnloaded");
 	_userstatsunloaded_t
 		.def_property_readonly("ptr", [](UserStatsUnloaded_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("steamIDUser", &UserStatsUnloaded_t::m_steamIDUser)
 	;
 	py::class_<UserAchievementIconFetched_t> _userachievementiconfetched_t(m, "UserAchievementIconFetched");
 	_userachievementiconfetched_t
 		.def_property_readonly("ptr", [](UserAchievementIconFetched_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("nGameID", &UserAchievementIconFetched_t::m_nGameID)
 		.def_property("rgchAchievementName",
			[](const UserAchievementIconFetched_t& x) { return get_char_array(x.m_rgchAchievementName); },
			[](UserAchievementIconFetched_t& x, const std::string& s) { set_char_array(x.m_rgchAchievementName, s); })
 		.def_readwrite("bAchieved", &UserAchievementIconFetched_t::m_bAchieved)
 		.def_readwrite("nIconHandle", &UserAchievementIconFetched_t::m_nIconHandle)
 	;
 	py::class_<GlobalAchievementPercentagesReady_t> _globalachievementpercentagesready_t(m, "GlobalAchievementPercentagesReady");
 	_globalachievementpercentagesready_t
 		.def_property_readonly("ptr", [](GlobalAchievementPercentagesReady_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("nGameID", &GlobalAchievementPercentagesReady_t::m_nGameID)
 		.def_readwrite("eResult", &GlobalAchievementPercentagesReady_t::m_eResult)
 	;
 	py::class_<LeaderboardUGCSet_t> _leaderboardugcset_t(m, "LeaderboardUGCSet");
 	_leaderboardugcset_t
 		.def_property_readonly("ptr", [](LeaderboardUGCSet_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("eResult", &LeaderboardUGCSet_t::m_eResult)
 		.def_readwrite("hSteamLeaderboard", &LeaderboardUGCSet_t::m_hSteamLeaderboard)
 	;
 	py::class_<GlobalStatsReceived_t> _globalstatsreceived_t(m, "GlobalStatsReceived");
 	_globalstatsreceived_t
 		.def_property_readonly("ptr", [](GlobalStatsReceived_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("nGameID", &GlobalStatsReceived_t::m_nGameID)
 		.def_readwrite("eResult", &GlobalStatsReceived_t::m_eResult)
 	;
 	py::class_<DlcInstalled_t> _dlcinstalled_t(m, "DlcInstalled");
 	_dlcinstalled_t
 		.def_property_readonly("ptr", [](DlcInstalled_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("nAppID", &DlcInstalled_t::m_nAppID)
 	;
 	py::class_<NewUrlLaunchParameters_t> _newurllaunchparameters_t(m, "NewUrlLaunchParameters");
 	_newurllaunchparameters_t
 		.def_property_readonly("ptr", [](NewUrlLaunchParameters_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 	;
 	py::class_<AppProofOfPurchaseKeyResponse_t> _appproofofpurchasekeyresponse_t(m, "AppProofOfPurchaseKeyResponse");
 	_appproofofpurchasekeyresponse_t
 		.def_property_readonly("ptr", [](AppProofOfPurchaseKeyResponse_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("eResult", &AppProofOfPurchaseKeyResponse_t::m_eResult)
 		.def_readwrite("nAppID", &AppProofOfPurchaseKeyResponse_t::m_nAppID)
 		.def_readwrite("cchKeyLength", &AppProofOfPurchaseKeyResponse_t::m_cchKeyLength)
 		.def_property("rgchKey",
			[](const AppProofOfPurchaseKeyResponse_t& x) { return get_char_array(x.m_rgchKey); },
			[](AppProofOfPurchaseKeyResponse_t& x, const std::string& s) { set_char_array(x.m_rgchKey, s); })
 	;
 	py::class_<FileDetailsResult_t> _filedetailsresult_t(m, "FileDetailsResult");
 	_filedetailsresult_t
 		.def_property_readonly("ptr", [](FileDetailsResult_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("eResult", &FileDetailsResult_t::m_eResult)
 		.def_readwrite("ulFileSize", &FileDetailsResult_t::m_ulFileSize)
 		.def_property_readonly("FileSHA",
			[](FileDetailsResult_t& x) { return FixedArrayView<uint8, 20>{ x.m_FileSHA }; },
			py::return_value_policy::reference_internal)
 		.def_readwrite("unFlags", &FileDetailsResult_t::m_unFlags)
 	;
 	py::class_<TimedTrialStatus_t> _timedtrialstatus_t(m, "TimedTrialStatus");
 	_timedtrialstatus_t
 		.def_property_readonly("ptr", [](TimedTrialStatus_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("unAppID", &TimedTrialStatus_t::m_unAppID)
 		.def_readwrite("bIsOffline", &TimedTrialStatus_t::m_bIsOffline)
 		.def_readwrite("unSecondsAllowed", &TimedTrialStatus_t::m_unSecondsAllowed)
 		.def_readwrite("unSecondsPlayed", &TimedTrialStatus_t::m_unSecondsPlayed)
 	;
 	py::class_<P2PSessionRequest_t> _p2psessionrequest_t(m, "P2PSessionRequest");
 	_p2psessionrequest_t
 		.def_property_readonly("ptr", [](P2PSessionRequest_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("steamIDRemote", &P2PSessionRequest_t::m_steamIDRemote)
 	;
 	py::class_<P2PSessionConnectFail_t> _p2psessionconnectfail_t(m, "P2PSessionConnectFail");
 	_p2psessionconnectfail_t
 		.def_property_readonly("ptr", [](P2PSessionConnectFail_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("steamIDRemote", &P2PSessionConnectFail_t::m_steamIDRemote)
 		.def_readwrite("eP2PSessionError", &P2PSessionConnectFail_t::m_eP2PSessionError)
 	;
 	py::class_<SocketStatusCallback_t> _socketstatuscallback_t(m, "SocketStatusCallback");
 	_socketstatuscallback_t
 		.def_property_readonly("ptr", [](SocketStatusCallback_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("hSocket", &SocketStatusCallback_t::m_hSocket)
 		.def_readwrite("hListenSocket", &SocketStatusCallback_t::m_hListenSocket)
 		.def_readwrite("steamIDRemote", &SocketStatusCallback_t::m_steamIDRemote)
 		.def_readwrite("eSNetSocketState", &SocketStatusCallback_t::m_eSNetSocketState)
 	;
 	py::class_<ScreenshotReady_t> _screenshotready_t(m, "ScreenshotReady");
 	_screenshotready_t
 		.def_property_readonly("ptr", [](ScreenshotReady_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("hLocal", &ScreenshotReady_t::m_hLocal)
 		.def_readwrite("eResult", &ScreenshotReady_t::m_eResult)
 	;
 	py::class_<ScreenshotRequested_t> _screenshotrequested_t(m, "ScreenshotRequested");
 	_screenshotrequested_t
 		.def_property_readonly("ptr", [](ScreenshotRequested_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 	;
 	py::class_<PlaybackStatusHasChanged_t> _playbackstatushaschanged_t(m, "PlaybackStatusHasChanged");
 	_playbackstatushaschanged_t
 		.def_property_readonly("ptr", [](PlaybackStatusHasChanged_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 	;
 	py::class_<VolumeHasChanged_t> _volumehaschanged_t(m, "VolumeHasChanged");
 	_volumehaschanged_t
 		.def_property_readonly("ptr", [](VolumeHasChanged_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("flNewVolume", &VolumeHasChanged_t::m_flNewVolume)
 	;
 	py::class_<HTTPRequestCompleted_t> _httprequestcompleted_t(m, "HTTPRequestCompleted");
 	_httprequestcompleted_t
 		.def_property_readonly("ptr", [](HTTPRequestCompleted_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("hRequest", &HTTPRequestCompleted_t::m_hRequest)
 		.def_readwrite("ulContextValue", &HTTPRequestCompleted_t::m_ulContextValue)
 		.def_readwrite("bRequestSuccessful", &HTTPRequestCompleted_t::m_bRequestSuccessful)
 		.def_readwrite("eStatusCode", &HTTPRequestCompleted_t::m_eStatusCode)
 		.def_readwrite("unBodySize", &HTTPRequestCompleted_t::m_unBodySize)
 	;
 	py::class_<HTTPRequestHeadersReceived_t> _httprequestheadersreceived_t(m, "HTTPRequestHeadersReceived");
 	_httprequestheadersreceived_t
 		.def_property_readonly("ptr", [](HTTPRequestHeadersReceived_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("hRequest", &HTTPRequestHeadersReceived_t::m_hRequest)
 		.def_readwrite("ulContextValue", &HTTPRequestHeadersReceived_t::m_ulContextValue)
 	;
 	py::class_<HTTPRequestDataReceived_t> _httprequestdatareceived_t(m, "HTTPRequestDataReceived");
 	_httprequestdatareceived_t
 		.def_property_readonly("ptr", [](HTTPRequestDataReceived_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("hRequest", &HTTPRequestDataReceived_t::m_hRequest)
 		.def_readwrite("ulContextValue", &HTTPRequestDataReceived_t::m_ulContextValue)
 		.def_readwrite("cOffset", &HTTPRequestDataReceived_t::m_cOffset)
 		.def_readwrite("cBytesReceived", &HTTPRequestDataReceived_t::m_cBytesReceived)
 	;
 	py::class_<SteamInputDeviceConnected_t> _steaminputdeviceconnected_t(m, "SteamInputDeviceConnected");
 	_steaminputdeviceconnected_t
 		.def_property_readonly("ptr", [](SteamInputDeviceConnected_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("ulConnectedDeviceHandle", &SteamInputDeviceConnected_t::m_ulConnectedDeviceHandle)
 	;
 	py::class_<SteamInputDeviceDisconnected_t> _steaminputdevicedisconnected_t(m, "SteamInputDeviceDisconnected");
 	_steaminputdevicedisconnected_t
 		.def_property_readonly("ptr", [](SteamInputDeviceDisconnected_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("ulDisconnectedDeviceHandle", &SteamInputDeviceDisconnected_t::m_ulDisconnectedDeviceHandle)
 	;
 	py::class_<SteamInputConfigurationLoaded_t> _steaminputconfigurationloaded_t(m, "SteamInputConfigurationLoaded");
 	_steaminputconfigurationloaded_t
 		.def_property_readonly("ptr", [](SteamInputConfigurationLoaded_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("unAppID", &SteamInputConfigurationLoaded_t::m_unAppID)
 		.def_readwrite("ulDeviceHandle", &SteamInputConfigurationLoaded_t::m_ulDeviceHandle)
 		.def_readwrite("ulMappingCreator", &SteamInputConfigurationLoaded_t::m_ulMappingCreator)
 		.def_readwrite("unMajorRevision", &SteamInputConfigurationLoaded_t::m_unMajorRevision)
 		.def_readwrite("unMinorRevision", &SteamInputConfigurationLoaded_t::m_unMinorRevision)
 		.def_readwrite("bUsesSteamInputAPI", &SteamInputConfigurationLoaded_t::m_bUsesSteamInputAPI)
 		.def_readwrite("bUsesGamepadAPI", &SteamInputConfigurationLoaded_t::m_bUsesGamepadAPI)
 	;
 	py::class_<SteamInputGamepadSlotChange_t> _steaminputgamepadslotchange_t(m, "SteamInputGamepadSlotChange");
 	_steaminputgamepadslotchange_t
 		.def_property_readonly("ptr", [](SteamInputGamepadSlotChange_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("unAppID", &SteamInputGamepadSlotChange_t::m_unAppID)
 		.def_readwrite("ulDeviceHandle", &SteamInputGamepadSlotChange_t::m_ulDeviceHandle)
 		.def_readwrite("eDeviceType", &SteamInputGamepadSlotChange_t::m_eDeviceType)
 		.def_readwrite("nOldGamepadSlot", &SteamInputGamepadSlotChange_t::m_nOldGamepadSlot)
 		.def_readwrite("nNewGamepadSlot", &SteamInputGamepadSlotChange_t::m_nNewGamepadSlot)
 	;
 	py::class_<SteamUGCQueryCompleted_t> _steamugcquerycompleted_t(m, "SteamUGCQueryCompleted");
 	_steamugcquerycompleted_t
 		.def_property_readonly("ptr", [](SteamUGCQueryCompleted_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("handle", &SteamUGCQueryCompleted_t::m_handle)
 		.def_readwrite("eResult", &SteamUGCQueryCompleted_t::m_eResult)
 		.def_readwrite("unNumResultsReturned", &SteamUGCQueryCompleted_t::m_unNumResultsReturned)
 		.def_readwrite("unTotalMatchingResults", &SteamUGCQueryCompleted_t::m_unTotalMatchingResults)
 		.def_readwrite("bCachedData", &SteamUGCQueryCompleted_t::m_bCachedData)
 		.def_property("rgchNextCursor",
			[](const SteamUGCQueryCompleted_t& x) { return get_char_array(x.m_rgchNextCursor); },
			[](SteamUGCQueryCompleted_t& x, const std::string& s) { set_char_array(x.m_rgchNextCursor, s); })
 	;
 	py::class_<SteamUGCRequestUGCDetailsResult_t> _steamugcrequestugcdetailsresult_t(m, "SteamUGCRequestUGCDetailsResult");
 	_steamugcrequestugcdetailsresult_t
 		.def_property_readonly("ptr", [](SteamUGCRequestUGCDetailsResult_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("details", &SteamUGCRequestUGCDetailsResult_t::m_details)
 		.def_readwrite("bCachedData", &SteamUGCRequestUGCDetailsResult_t::m_bCachedData)
 	;
 	py::class_<CreateItemResult_t> _createitemresult_t(m, "CreateItemResult");
 	_createitemresult_t
 		.def_property_readonly("ptr", [](CreateItemResult_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("eResult", &CreateItemResult_t::m_eResult)
 		.def_readwrite("nPublishedFileId", &CreateItemResult_t::m_nPublishedFileId)
 		.def_readwrite("bUserNeedsToAcceptWorkshopLegalAgreement", &CreateItemResult_t::m_bUserNeedsToAcceptWorkshopLegalAgreement)
 	;
 	py::class_<SubmitItemUpdateResult_t> _submititemupdateresult_t(m, "SubmitItemUpdateResult");
 	_submititemupdateresult_t
 		.def_property_readonly("ptr", [](SubmitItemUpdateResult_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("eResult", &SubmitItemUpdateResult_t::m_eResult)
 		.def_readwrite("bUserNeedsToAcceptWorkshopLegalAgreement", &SubmitItemUpdateResult_t::m_bUserNeedsToAcceptWorkshopLegalAgreement)
 		.def_readwrite("nPublishedFileId", &SubmitItemUpdateResult_t::m_nPublishedFileId)
 	;
 	py::class_<ItemInstalled_t> _iteminstalled_t(m, "ItemInstalled");
 	_iteminstalled_t
 		.def_property_readonly("ptr", [](ItemInstalled_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("unAppID", &ItemInstalled_t::m_unAppID)
 		.def_readwrite("nPublishedFileId", &ItemInstalled_t::m_nPublishedFileId)
 		.def_readwrite("hLegacyContent", &ItemInstalled_t::m_hLegacyContent)
 		.def_readwrite("unManifestID", &ItemInstalled_t::m_unManifestID)
 	;
 	py::class_<DownloadItemResult_t> _downloaditemresult_t(m, "DownloadItemResult");
 	_downloaditemresult_t
 		.def_property_readonly("ptr", [](DownloadItemResult_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("unAppID", &DownloadItemResult_t::m_unAppID)
 		.def_readwrite("nPublishedFileId", &DownloadItemResult_t::m_nPublishedFileId)
 		.def_readwrite("eResult", &DownloadItemResult_t::m_eResult)
 	;
 	py::class_<UserFavoriteItemsListChanged_t> _userfavoriteitemslistchanged_t(m, "UserFavoriteItemsListChanged");
 	_userfavoriteitemslistchanged_t
 		.def_property_readonly("ptr", [](UserFavoriteItemsListChanged_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("nPublishedFileId", &UserFavoriteItemsListChanged_t::m_nPublishedFileId)
 		.def_readwrite("eResult", &UserFavoriteItemsListChanged_t::m_eResult)
 		.def_readwrite("bWasAddRequest", &UserFavoriteItemsListChanged_t::m_bWasAddRequest)
 	;
 	py::class_<SetUserItemVoteResult_t> _setuseritemvoteresult_t(m, "SetUserItemVoteResult");
 	_setuseritemvoteresult_t
 		.def_property_readonly("ptr", [](SetUserItemVoteResult_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("nPublishedFileId", &SetUserItemVoteResult_t::m_nPublishedFileId)
 		.def_readwrite("eResult", &SetUserItemVoteResult_t::m_eResult)
 		.def_readwrite("bVoteUp", &SetUserItemVoteResult_t::m_bVoteUp)
 	;
 	py::class_<GetUserItemVoteResult_t> _getuseritemvoteresult_t(m, "GetUserItemVoteResult");
 	_getuseritemvoteresult_t
 		.def_property_readonly("ptr", [](GetUserItemVoteResult_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("nPublishedFileId", &GetUserItemVoteResult_t::m_nPublishedFileId)
 		.def_readwrite("eResult", &GetUserItemVoteResult_t::m_eResult)
 		.def_readwrite("bVotedUp", &GetUserItemVoteResult_t::m_bVotedUp)
 		.def_readwrite("bVotedDown", &GetUserItemVoteResult_t::m_bVotedDown)
 		.def_readwrite("bVoteSkipped", &GetUserItemVoteResult_t::m_bVoteSkipped)
 	;
 	py::class_<StartPlaytimeTrackingResult_t> _startplaytimetrackingresult_t(m, "StartPlaytimeTrackingResult");
 	_startplaytimetrackingresult_t
 		.def_property_readonly("ptr", [](StartPlaytimeTrackingResult_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("eResult", &StartPlaytimeTrackingResult_t::m_eResult)
 	;
 	py::class_<StopPlaytimeTrackingResult_t> _stopplaytimetrackingresult_t(m, "StopPlaytimeTrackingResult");
 	_stopplaytimetrackingresult_t
 		.def_property_readonly("ptr", [](StopPlaytimeTrackingResult_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("eResult", &StopPlaytimeTrackingResult_t::m_eResult)
 	;
 	py::class_<AddUGCDependencyResult_t> _addugcdependencyresult_t(m, "AddUGCDependencyResult");
 	_addugcdependencyresult_t
 		.def_property_readonly("ptr", [](AddUGCDependencyResult_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("eResult", &AddUGCDependencyResult_t::m_eResult)
 		.def_readwrite("nPublishedFileId", &AddUGCDependencyResult_t::m_nPublishedFileId)
 		.def_readwrite("nChildPublishedFileId", &AddUGCDependencyResult_t::m_nChildPublishedFileId)
 	;
 	py::class_<RemoveUGCDependencyResult_t> _removeugcdependencyresult_t(m, "RemoveUGCDependencyResult");
 	_removeugcdependencyresult_t
 		.def_property_readonly("ptr", [](RemoveUGCDependencyResult_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("eResult", &RemoveUGCDependencyResult_t::m_eResult)
 		.def_readwrite("nPublishedFileId", &RemoveUGCDependencyResult_t::m_nPublishedFileId)
 		.def_readwrite("nChildPublishedFileId", &RemoveUGCDependencyResult_t::m_nChildPublishedFileId)
 	;
 	py::class_<AddAppDependencyResult_t> _addappdependencyresult_t(m, "AddAppDependencyResult");
 	_addappdependencyresult_t
 		.def_property_readonly("ptr", [](AddAppDependencyResult_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("eResult", &AddAppDependencyResult_t::m_eResult)
 		.def_readwrite("nPublishedFileId", &AddAppDependencyResult_t::m_nPublishedFileId)
 		.def_readwrite("nAppID", &AddAppDependencyResult_t::m_nAppID)
 	;
 	py::class_<RemoveAppDependencyResult_t> _removeappdependencyresult_t(m, "RemoveAppDependencyResult");
 	_removeappdependencyresult_t
 		.def_property_readonly("ptr", [](RemoveAppDependencyResult_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("eResult", &RemoveAppDependencyResult_t::m_eResult)
 		.def_readwrite("nPublishedFileId", &RemoveAppDependencyResult_t::m_nPublishedFileId)
 		.def_readwrite("nAppID", &RemoveAppDependencyResult_t::m_nAppID)
 	;
 	py::class_<GetAppDependenciesResult_t> _getappdependenciesresult_t(m, "GetAppDependenciesResult");
 	_getappdependenciesresult_t
 		.def_property_readonly("ptr", [](GetAppDependenciesResult_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("eResult", &GetAppDependenciesResult_t::m_eResult)
 		.def_readwrite("nPublishedFileId", &GetAppDependenciesResult_t::m_nPublishedFileId)
 		.def_property_readonly("rgAppIDs",
			[](GetAppDependenciesResult_t& x) { return FixedArrayView<AppId_t, 32>{ x.m_rgAppIDs }; },
			py::return_value_policy::reference_internal)
 		.def_readwrite("nNumAppDependencies", &GetAppDependenciesResult_t::m_nNumAppDependencies)
 		.def_readwrite("nTotalNumAppDependencies", &GetAppDependenciesResult_t::m_nTotalNumAppDependencies)
 	;
 	py::class_<DeleteItemResult_t> _deleteitemresult_t(m, "DeleteItemResult");
 	_deleteitemresult_t
 		.def_property_readonly("ptr", [](DeleteItemResult_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("eResult", &DeleteItemResult_t::m_eResult)
 		.def_readwrite("nPublishedFileId", &DeleteItemResult_t::m_nPublishedFileId)
 	;
 	py::class_<UserSubscribedItemsListChanged_t> _usersubscribeditemslistchanged_t(m, "UserSubscribedItemsListChanged");
 	_usersubscribeditemslistchanged_t
 		.def_property_readonly("ptr", [](UserSubscribedItemsListChanged_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("nAppID", &UserSubscribedItemsListChanged_t::m_nAppID)
 	;
 	py::class_<WorkshopEULAStatus_t> _workshopeulastatus_t(m, "WorkshopEULAStatus");
 	_workshopeulastatus_t
 		.def_property_readonly("ptr", [](WorkshopEULAStatus_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("eResult", &WorkshopEULAStatus_t::m_eResult)
 		.def_readwrite("nAppID", &WorkshopEULAStatus_t::m_nAppID)
 		.def_readwrite("unVersion", &WorkshopEULAStatus_t::m_unVersion)
 		.def_readwrite("rtAction", &WorkshopEULAStatus_t::m_rtAction)
 		.def_readwrite("bAccepted", &WorkshopEULAStatus_t::m_bAccepted)
 		.def_readwrite("bNeedsAction", &WorkshopEULAStatus_t::m_bNeedsAction)
 	;
 	py::class_<HTML_BrowserReady_t> _html_browserready_t(m, "HTML_BrowserReady");
 	_html_browserready_t
 		.def_property_readonly("ptr", [](HTML_BrowserReady_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("unBrowserHandle", &HTML_BrowserReady_t::unBrowserHandle)
 	;
 	py::class_<HTML_NeedsPaint_t> _html_needspaint_t(m, "HTML_NeedsPaint");
 	_html_needspaint_t
 		.def_property_readonly("ptr", [](HTML_NeedsPaint_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("unBrowserHandle", &HTML_NeedsPaint_t::unBrowserHandle)
 		.def_readwrite("pBGRA", &HTML_NeedsPaint_t::pBGRA)
 		.def_readwrite("unWide", &HTML_NeedsPaint_t::unWide)
 		.def_readwrite("unTall", &HTML_NeedsPaint_t::unTall)
 		.def_readwrite("unUpdateX", &HTML_NeedsPaint_t::unUpdateX)
 		.def_readwrite("unUpdateY", &HTML_NeedsPaint_t::unUpdateY)
 		.def_readwrite("unUpdateWide", &HTML_NeedsPaint_t::unUpdateWide)
 		.def_readwrite("unUpdateTall", &HTML_NeedsPaint_t::unUpdateTall)
 		.def_readwrite("unScrollX", &HTML_NeedsPaint_t::unScrollX)
 		.def_readwrite("unScrollY", &HTML_NeedsPaint_t::unScrollY)
 		.def_readwrite("flPageScale", &HTML_NeedsPaint_t::flPageScale)
 		.def_readwrite("unPageSerial", &HTML_NeedsPaint_t::unPageSerial)
 	;
 	py::class_<HTML_StartRequest_t> _html_startrequest_t(m, "HTML_StartRequest");
 	_html_startrequest_t
 		.def_property_readonly("ptr", [](HTML_StartRequest_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("unBrowserHandle", &HTML_StartRequest_t::unBrowserHandle)
 		.def_readwrite("pchURL", &HTML_StartRequest_t::pchURL)
 		.def_readwrite("pchTarget", &HTML_StartRequest_t::pchTarget)
 		.def_readwrite("pchPostData", &HTML_StartRequest_t::pchPostData)
 		.def_readwrite("bIsRedirect", &HTML_StartRequest_t::bIsRedirect)
 	;
 	py::class_<HTML_CloseBrowser_t> _html_closebrowser_t(m, "HTML_CloseBrowser");
 	_html_closebrowser_t
 		.def_property_readonly("ptr", [](HTML_CloseBrowser_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("unBrowserHandle", &HTML_CloseBrowser_t::unBrowserHandle)
 	;
 	py::class_<HTML_URLChanged_t> _html_urlchanged_t(m, "HTML_URLChanged");
 	_html_urlchanged_t
 		.def_property_readonly("ptr", [](HTML_URLChanged_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("unBrowserHandle", &HTML_URLChanged_t::unBrowserHandle)
 		.def_readwrite("pchURL", &HTML_URLChanged_t::pchURL)
 		.def_readwrite("pchPostData", &HTML_URLChanged_t::pchPostData)
 		.def_readwrite("bIsRedirect", &HTML_URLChanged_t::bIsRedirect)
 		.def_readwrite("pchPageTitle", &HTML_URLChanged_t::pchPageTitle)
 		.def_readwrite("bNewNavigation", &HTML_URLChanged_t::bNewNavigation)
 	;
 	py::class_<HTML_FinishedRequest_t> _html_finishedrequest_t(m, "HTML_FinishedRequest");
 	_html_finishedrequest_t
 		.def_property_readonly("ptr", [](HTML_FinishedRequest_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("unBrowserHandle", &HTML_FinishedRequest_t::unBrowserHandle)
 		.def_readwrite("pchURL", &HTML_FinishedRequest_t::pchURL)
 		.def_readwrite("pchPageTitle", &HTML_FinishedRequest_t::pchPageTitle)
 	;
 	py::class_<HTML_OpenLinkInNewTab_t> _html_openlinkinnewtab_t(m, "HTML_OpenLinkInNewTab");
 	_html_openlinkinnewtab_t
 		.def_property_readonly("ptr", [](HTML_OpenLinkInNewTab_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("unBrowserHandle", &HTML_OpenLinkInNewTab_t::unBrowserHandle)
 		.def_readwrite("pchURL", &HTML_OpenLinkInNewTab_t::pchURL)
 	;
 	py::class_<HTML_ChangedTitle_t> _html_changedtitle_t(m, "HTML_ChangedTitle");
 	_html_changedtitle_t
 		.def_property_readonly("ptr", [](HTML_ChangedTitle_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("unBrowserHandle", &HTML_ChangedTitle_t::unBrowserHandle)
 		.def_readwrite("pchTitle", &HTML_ChangedTitle_t::pchTitle)
 	;
 	py::class_<HTML_SearchResults_t> _html_searchresults_t(m, "HTML_SearchResults");
 	_html_searchresults_t
 		.def_property_readonly("ptr", [](HTML_SearchResults_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("unBrowserHandle", &HTML_SearchResults_t::unBrowserHandle)
 		.def_readwrite("unResults", &HTML_SearchResults_t::unResults)
 		.def_readwrite("unCurrentMatch", &HTML_SearchResults_t::unCurrentMatch)
 	;
 	py::class_<HTML_CanGoBackAndForward_t> _html_cangobackandforward_t(m, "HTML_CanGoBackAndForward");
 	_html_cangobackandforward_t
 		.def_property_readonly("ptr", [](HTML_CanGoBackAndForward_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("unBrowserHandle", &HTML_CanGoBackAndForward_t::unBrowserHandle)
 		.def_readwrite("bCanGoBack", &HTML_CanGoBackAndForward_t::bCanGoBack)
 		.def_readwrite("bCanGoForward", &HTML_CanGoBackAndForward_t::bCanGoForward)
 	;
 	py::class_<HTML_HorizontalScroll_t> _html_horizontalscroll_t(m, "HTML_HorizontalScroll");
 	_html_horizontalscroll_t
 		.def_property_readonly("ptr", [](HTML_HorizontalScroll_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("unBrowserHandle", &HTML_HorizontalScroll_t::unBrowserHandle)
 		.def_readwrite("unScrollMax", &HTML_HorizontalScroll_t::unScrollMax)
 		.def_readwrite("unScrollCurrent", &HTML_HorizontalScroll_t::unScrollCurrent)
 		.def_readwrite("flPageScale", &HTML_HorizontalScroll_t::flPageScale)
 		.def_readwrite("bVisible", &HTML_HorizontalScroll_t::bVisible)
 		.def_readwrite("unPageSize", &HTML_HorizontalScroll_t::unPageSize)
 	;
 	py::class_<HTML_VerticalScroll_t> _html_verticalscroll_t(m, "HTML_VerticalScroll");
 	_html_verticalscroll_t
 		.def_property_readonly("ptr", [](HTML_VerticalScroll_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("unBrowserHandle", &HTML_VerticalScroll_t::unBrowserHandle)
 		.def_readwrite("unScrollMax", &HTML_VerticalScroll_t::unScrollMax)
 		.def_readwrite("unScrollCurrent", &HTML_VerticalScroll_t::unScrollCurrent)
 		.def_readwrite("flPageScale", &HTML_VerticalScroll_t::flPageScale)
 		.def_readwrite("bVisible", &HTML_VerticalScroll_t::bVisible)
 		.def_readwrite("unPageSize", &HTML_VerticalScroll_t::unPageSize)
 	;
 	py::class_<HTML_LinkAtPosition_t> _html_linkatposition_t(m, "HTML_LinkAtPosition");
 	_html_linkatposition_t
 		.def_property_readonly("ptr", [](HTML_LinkAtPosition_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("unBrowserHandle", &HTML_LinkAtPosition_t::unBrowserHandle)
 		.def_readwrite("x", &HTML_LinkAtPosition_t::x)
 		.def_readwrite("y", &HTML_LinkAtPosition_t::y)
 		.def_readwrite("pchURL", &HTML_LinkAtPosition_t::pchURL)
 		.def_readwrite("bInput", &HTML_LinkAtPosition_t::bInput)
 		.def_readwrite("bLiveLink", &HTML_LinkAtPosition_t::bLiveLink)
 	;
 	py::class_<HTML_JSAlert_t> _html_jsalert_t(m, "HTML_JSAlert");
 	_html_jsalert_t
 		.def_property_readonly("ptr", [](HTML_JSAlert_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("unBrowserHandle", &HTML_JSAlert_t::unBrowserHandle)
 		.def_readwrite("pchMessage", &HTML_JSAlert_t::pchMessage)
 	;
 	py::class_<HTML_JSConfirm_t> _html_jsconfirm_t(m, "HTML_JSConfirm");
 	_html_jsconfirm_t
 		.def_property_readonly("ptr", [](HTML_JSConfirm_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("unBrowserHandle", &HTML_JSConfirm_t::unBrowserHandle)
 		.def_readwrite("pchMessage", &HTML_JSConfirm_t::pchMessage)
 	;
 	py::class_<HTML_FileOpenDialog_t> _html_fileopendialog_t(m, "HTML_FileOpenDialog");
 	_html_fileopendialog_t
 		.def_property_readonly("ptr", [](HTML_FileOpenDialog_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("unBrowserHandle", &HTML_FileOpenDialog_t::unBrowserHandle)
 		.def_readwrite("pchTitle", &HTML_FileOpenDialog_t::pchTitle)
 		.def_readwrite("pchInitialFile", &HTML_FileOpenDialog_t::pchInitialFile)
 	;
 	py::class_<HTML_NewWindow_t> _html_newwindow_t(m, "HTML_NewWindow");
 	_html_newwindow_t
 		.def_property_readonly("ptr", [](HTML_NewWindow_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("unBrowserHandle", &HTML_NewWindow_t::unBrowserHandle)
 		.def_readwrite("pchURL", &HTML_NewWindow_t::pchURL)
 		.def_readwrite("unX", &HTML_NewWindow_t::unX)
 		.def_readwrite("unY", &HTML_NewWindow_t::unY)
 		.def_readwrite("unWide", &HTML_NewWindow_t::unWide)
 		.def_readwrite("unTall", &HTML_NewWindow_t::unTall)
 		.def_readwrite("unNewWindow_BrowserHandle_IGNORE", &HTML_NewWindow_t::unNewWindow_BrowserHandle_IGNORE)
 	;
 	py::class_<HTML_SetCursor_t> _html_setcursor_t(m, "HTML_SetCursor");
 	_html_setcursor_t
 		.def_property_readonly("ptr", [](HTML_SetCursor_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("unBrowserHandle", &HTML_SetCursor_t::unBrowserHandle)
 		.def_readwrite("eMouseCursor", &HTML_SetCursor_t::eMouseCursor)
 	;
 	py::class_<HTML_StatusText_t> _html_statustext_t(m, "HTML_StatusText");
 	_html_statustext_t
 		.def_property_readonly("ptr", [](HTML_StatusText_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("unBrowserHandle", &HTML_StatusText_t::unBrowserHandle)
 		.def_readwrite("pchMsg", &HTML_StatusText_t::pchMsg)
 	;
 	py::class_<HTML_ShowToolTip_t> _html_showtooltip_t(m, "HTML_ShowToolTip");
 	_html_showtooltip_t
 		.def_property_readonly("ptr", [](HTML_ShowToolTip_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("unBrowserHandle", &HTML_ShowToolTip_t::unBrowserHandle)
 		.def_readwrite("pchMsg", &HTML_ShowToolTip_t::pchMsg)
 	;
 	py::class_<HTML_UpdateToolTip_t> _html_updatetooltip_t(m, "HTML_UpdateToolTip");
 	_html_updatetooltip_t
 		.def_property_readonly("ptr", [](HTML_UpdateToolTip_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("unBrowserHandle", &HTML_UpdateToolTip_t::unBrowserHandle)
 		.def_readwrite("pchMsg", &HTML_UpdateToolTip_t::pchMsg)
 	;
 	py::class_<HTML_HideToolTip_t> _html_hidetooltip_t(m, "HTML_HideToolTip");
 	_html_hidetooltip_t
 		.def_property_readonly("ptr", [](HTML_HideToolTip_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("unBrowserHandle", &HTML_HideToolTip_t::unBrowserHandle)
 	;
 	py::class_<HTML_BrowserRestarted_t> _html_browserrestarted_t(m, "HTML_BrowserRestarted");
 	_html_browserrestarted_t
 		.def_property_readonly("ptr", [](HTML_BrowserRestarted_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("unBrowserHandle", &HTML_BrowserRestarted_t::unBrowserHandle)
 		.def_readwrite("unOldBrowserHandle", &HTML_BrowserRestarted_t::unOldBrowserHandle)
 	;
 	py::class_<SteamInventoryResultReady_t> _steaminventoryresultready_t(m, "SteamInventoryResultReady");
 	_steaminventoryresultready_t
 		.def_property_readonly("ptr", [](SteamInventoryResultReady_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("handle", &SteamInventoryResultReady_t::m_handle)
 		.def_readwrite("result", &SteamInventoryResultReady_t::m_result)
 	;
 	py::class_<SteamInventoryFullUpdate_t> _steaminventoryfullupdate_t(m, "SteamInventoryFullUpdate");
 	_steaminventoryfullupdate_t
 		.def_property_readonly("ptr", [](SteamInventoryFullUpdate_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("handle", &SteamInventoryFullUpdate_t::m_handle)
 	;
 	py::class_<SteamInventoryDefinitionUpdate_t> _steaminventorydefinitionupdate_t(m, "SteamInventoryDefinitionUpdate");
 	_steaminventorydefinitionupdate_t
 		.def_property_readonly("ptr", [](SteamInventoryDefinitionUpdate_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 	;
 	py::class_<SteamInventoryEligiblePromoItemDefIDs_t> _steaminventoryeligiblepromoitemdefids_t(m, "SteamInventoryEligiblePromoItemDefIDs");
 	_steaminventoryeligiblepromoitemdefids_t
 		.def_property_readonly("ptr", [](SteamInventoryEligiblePromoItemDefIDs_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("result", &SteamInventoryEligiblePromoItemDefIDs_t::m_result)
 		.def_readwrite("steamID", &SteamInventoryEligiblePromoItemDefIDs_t::m_steamID)
 		.def_readwrite("numEligiblePromoItemDefs", &SteamInventoryEligiblePromoItemDefIDs_t::m_numEligiblePromoItemDefs)
 		.def_readwrite("bCachedData", &SteamInventoryEligiblePromoItemDefIDs_t::m_bCachedData)
 	;
 	py::class_<SteamInventoryStartPurchaseResult_t> _steaminventorystartpurchaseresult_t(m, "SteamInventoryStartPurchaseResult");
 	_steaminventorystartpurchaseresult_t
 		.def_property_readonly("ptr", [](SteamInventoryStartPurchaseResult_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("result", &SteamInventoryStartPurchaseResult_t::m_result)
 		.def_readwrite("ulOrderID", &SteamInventoryStartPurchaseResult_t::m_ulOrderID)
 		.def_readwrite("ulTransID", &SteamInventoryStartPurchaseResult_t::m_ulTransID)
 	;
 	py::class_<SteamInventoryRequestPricesResult_t> _steaminventoryrequestpricesresult_t(m, "SteamInventoryRequestPricesResult");
 	_steaminventoryrequestpricesresult_t
 		.def_property_readonly("ptr", [](SteamInventoryRequestPricesResult_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("result", &SteamInventoryRequestPricesResult_t::m_result)
 		.def_property("rgchCurrency",
			[](const SteamInventoryRequestPricesResult_t& x) { return get_char_array(x.m_rgchCurrency); },
			[](SteamInventoryRequestPricesResult_t& x, const std::string& s) { set_char_array(x.m_rgchCurrency, s); })
 	;
 	py::class_<SteamTimelineGamePhaseRecordingExists_t> _steamtimelinegamephaserecordingexists_t(m, "SteamTimelineGamePhaseRecordingExists");
 	_steamtimelinegamephaserecordingexists_t
 		.def_property_readonly("ptr", [](SteamTimelineGamePhaseRecordingExists_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_property("rgchPhaseID",
			[](const SteamTimelineGamePhaseRecordingExists_t& x) { return get_char_array(x.m_rgchPhaseID); },
			[](SteamTimelineGamePhaseRecordingExists_t& x, const std::string& s) { set_char_array(x.m_rgchPhaseID, s); })
 		.def_readwrite("ulRecordingMS", &SteamTimelineGamePhaseRecordingExists_t::m_ulRecordingMS)
 		.def_readwrite("ulLongestClipMS", &SteamTimelineGamePhaseRecordingExists_t::m_ulLongestClipMS)
 		.def_readwrite("unClipCount", &SteamTimelineGamePhaseRecordingExists_t::m_unClipCount)
 		.def_readwrite("unScreenshotCount", &SteamTimelineGamePhaseRecordingExists_t::m_unScreenshotCount)
 	;
 	py::class_<SteamTimelineEventRecordingExists_t> _steamtimelineeventrecordingexists_t(m, "SteamTimelineEventRecordingExists");
 	_steamtimelineeventrecordingexists_t
 		.def_property_readonly("ptr", [](SteamTimelineEventRecordingExists_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("ulEventID", &SteamTimelineEventRecordingExists_t::m_ulEventID)
 		.def_readwrite("bRecordingExists", &SteamTimelineEventRecordingExists_t::m_bRecordingExists)
 	;
 	py::class_<GetVideoURLResult_t> _getvideourlresult_t(m, "GetVideoURLResult");
 	_getvideourlresult_t
 		.def_property_readonly("ptr", [](GetVideoURLResult_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("eResult", &GetVideoURLResult_t::m_eResult)
 		.def_readwrite("unVideoAppID", &GetVideoURLResult_t::m_unVideoAppID)
 		.def_property("rgchURL",
			[](const GetVideoURLResult_t& x) { return get_char_array(x.m_rgchURL); },
			[](GetVideoURLResult_t& x, const std::string& s) { set_char_array(x.m_rgchURL, s); })
 	;
 	py::class_<GetOPFSettingsResult_t> _getopfsettingsresult_t(m, "GetOPFSettingsResult");
 	_getopfsettingsresult_t
 		.def_property_readonly("ptr", [](GetOPFSettingsResult_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("eResult", &GetOPFSettingsResult_t::m_eResult)
 		.def_readwrite("unVideoAppID", &GetOPFSettingsResult_t::m_unVideoAppID)
 	;
 	py::class_<BroadcastUploadStart_t> _broadcastuploadstart_t(m, "BroadcastUploadStart");
 	_broadcastuploadstart_t
 		.def_property_readonly("ptr", [](BroadcastUploadStart_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("bIsRTMP", &BroadcastUploadStart_t::m_bIsRTMP)
 	;
 	py::class_<BroadcastUploadStop_t> _broadcastuploadstop_t(m, "BroadcastUploadStop");
 	_broadcastuploadstop_t
 		.def_property_readonly("ptr", [](BroadcastUploadStop_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("eResult", &BroadcastUploadStop_t::m_eResult)
 	;
 	py::class_<SteamParentalSettingsChanged_t> _steamparentalsettingschanged_t(m, "SteamParentalSettingsChanged");
 	_steamparentalsettingschanged_t
 		.def_property_readonly("ptr", [](SteamParentalSettingsChanged_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 	;
 	py::class_<SteamRemotePlaySessionConnected_t> _steamremoteplaysessionconnected_t(m, "SteamRemotePlaySessionConnected");
 	_steamremoteplaysessionconnected_t
 		.def_property_readonly("ptr", [](SteamRemotePlaySessionConnected_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("unSessionID", &SteamRemotePlaySessionConnected_t::m_unSessionID)
 	;
 	py::class_<SteamRemotePlaySessionDisconnected_t> _steamremoteplaysessiondisconnected_t(m, "SteamRemotePlaySessionDisconnected");
 	_steamremoteplaysessiondisconnected_t
 		.def_property_readonly("ptr", [](SteamRemotePlaySessionDisconnected_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("unSessionID", &SteamRemotePlaySessionDisconnected_t::m_unSessionID)
 	;
 	py::class_<SteamRemotePlayTogetherGuestInvite_t> _steamremoteplaytogetherguestinvite_t(m, "SteamRemotePlayTogetherGuestInvite");
 	_steamremoteplaytogetherguestinvite_t
 		.def_property_readonly("ptr", [](SteamRemotePlayTogetherGuestInvite_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_property("szConnectURL",
			[](const SteamRemotePlayTogetherGuestInvite_t& x) { return get_char_array(x.m_szConnectURL); },
			[](SteamRemotePlayTogetherGuestInvite_t& x, const std::string& s) { set_char_array(x.m_szConnectURL, s); })
 	;
 	py::class_<SteamRemotePlaySessionAvatarLoaded_t> _steamremoteplaysessionavatarloaded_t(m, "SteamRemotePlaySessionAvatarLoaded");
 	_steamremoteplaysessionavatarloaded_t
 		.def_property_readonly("ptr", [](SteamRemotePlaySessionAvatarLoaded_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("unSessionID", &SteamRemotePlaySessionAvatarLoaded_t::m_unSessionID)
 		.def_readwrite("iImage", &SteamRemotePlaySessionAvatarLoaded_t::m_iImage)
 		.def_readwrite("iWide", &SteamRemotePlaySessionAvatarLoaded_t::m_iWide)
 		.def_readwrite("iTall", &SteamRemotePlaySessionAvatarLoaded_t::m_iTall)
 	;
 	py::class_<SteamNetworkingMessagesSessionRequest_t> _steamnetworkingmessagessessionrequest_t(m, "SteamNetworkingMessagesSessionRequest");
 	_steamnetworkingmessagessessionrequest_t
 		.def_property_readonly("ptr", [](SteamNetworkingMessagesSessionRequest_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("identityRemote", &SteamNetworkingMessagesSessionRequest_t::m_identityRemote)
 	;
 	py::class_<SteamNetworkingMessagesSessionFailed_t> _steamnetworkingmessagessessionfailed_t(m, "SteamNetworkingMessagesSessionFailed");
 	_steamnetworkingmessagessessionfailed_t
 		.def_property_readonly("ptr", [](SteamNetworkingMessagesSessionFailed_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("info", &SteamNetworkingMessagesSessionFailed_t::m_info)
 	;
 	py::class_<SteamNetConnectionStatusChangedCallback_t> _steamnetconnectionstatuschangedcallback_t(m, "SteamNetConnectionStatusChangedCallback");
 	_steamnetconnectionstatuschangedcallback_t
 		.def_property_readonly("ptr", [](SteamNetConnectionStatusChangedCallback_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("hConn", &SteamNetConnectionStatusChangedCallback_t::m_hConn)
 		.def_readwrite("info", &SteamNetConnectionStatusChangedCallback_t::m_info)
 		.def_readwrite("eOldState", &SteamNetConnectionStatusChangedCallback_t::m_eOldState)
 	;
 	py::class_<SteamNetAuthenticationStatus_t> _steamnetauthenticationstatus_t(m, "SteamNetAuthenticationStatus");
 	_steamnetauthenticationstatus_t
 		.def_property_readonly("ptr", [](SteamNetAuthenticationStatus_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("eAvail", &SteamNetAuthenticationStatus_t::m_eAvail)
 		.def_property("debugMsg",
			[](const SteamNetAuthenticationStatus_t& x) { return get_char_array(x.m_debugMsg); },
			[](SteamNetAuthenticationStatus_t& x, const std::string& s) { set_char_array(x.m_debugMsg, s); })
 	;
 	py::class_<SteamRelayNetworkStatus_t> _steamrelaynetworkstatus_t(m, "SteamRelayNetworkStatus");
 	_steamrelaynetworkstatus_t
 		.def_property_readonly("ptr", [](SteamRelayNetworkStatus_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("eAvail", &SteamRelayNetworkStatus_t::m_eAvail)
 		.def_readwrite("bPingMeasurementInProgress", &SteamRelayNetworkStatus_t::m_bPingMeasurementInProgress)
 		.def_readwrite("eAvailNetworkConfig", &SteamRelayNetworkStatus_t::m_eAvailNetworkConfig)
 		.def_readwrite("eAvailAnyRelay", &SteamRelayNetworkStatus_t::m_eAvailAnyRelay)
 		.def_property("debugMsg",
			[](const SteamRelayNetworkStatus_t& x) { return get_char_array(x.m_debugMsg); },
			[](SteamRelayNetworkStatus_t& x, const std::string& s) { set_char_array(x.m_debugMsg, s); })
 	;
 	py::class_<GSClientApprove_t> _gsclientapprove_t(m, "GSClientApprove");
 	_gsclientapprove_t
 		.def_property_readonly("ptr", [](GSClientApprove_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("SteamID", &GSClientApprove_t::m_SteamID)
 		.def_readwrite("OwnerSteamID", &GSClientApprove_t::m_OwnerSteamID)
 	;
 	py::class_<GSClientDeny_t> _gsclientdeny_t(m, "GSClientDeny");
 	_gsclientdeny_t
 		.def_property_readonly("ptr", [](GSClientDeny_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("SteamID", &GSClientDeny_t::m_SteamID)
 		.def_readwrite("eDenyReason", &GSClientDeny_t::m_eDenyReason)
 		.def_property("rgchOptionalText",
			[](const GSClientDeny_t& x) { return get_char_array(x.m_rgchOptionalText); },
			[](GSClientDeny_t& x, const std::string& s) { set_char_array(x.m_rgchOptionalText, s); })
 	;
 	py::class_<GSClientKick_t> _gsclientkick_t(m, "GSClientKick");
 	_gsclientkick_t
 		.def_property_readonly("ptr", [](GSClientKick_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("SteamID", &GSClientKick_t::m_SteamID)
 		.def_readwrite("eDenyReason", &GSClientKick_t::m_eDenyReason)
 	;
 	py::class_<GSClientAchievementStatus_t> _gsclientachievementstatus_t(m, "GSClientAchievementStatus");
 	_gsclientachievementstatus_t
 		.def_property_readonly("ptr", [](GSClientAchievementStatus_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("SteamID", &GSClientAchievementStatus_t::m_SteamID)
 		.def_property("pchAchievement",
			[](const GSClientAchievementStatus_t& x) { return get_char_array(x.m_pchAchievement); },
			[](GSClientAchievementStatus_t& x, const std::string& s) { set_char_array(x.m_pchAchievement, s); })
 		.def_readwrite("bUnlocked", &GSClientAchievementStatus_t::m_bUnlocked)
 	;
 	py::class_<GSPolicyResponse_t> _gspolicyresponse_t(m, "GSPolicyResponse");
 	_gspolicyresponse_t
 		.def_property_readonly("ptr", [](GSPolicyResponse_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("bSecure", &GSPolicyResponse_t::m_bSecure)
 	;
 	py::class_<GSGameplayStats_t> _gsgameplaystats_t(m, "GSGameplayStats");
 	_gsgameplaystats_t
 		.def_property_readonly("ptr", [](GSGameplayStats_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("eResult", &GSGameplayStats_t::m_eResult)
 		.def_readwrite("nRank", &GSGameplayStats_t::m_nRank)
 		.def_readwrite("unTotalConnects", &GSGameplayStats_t::m_unTotalConnects)
 		.def_readwrite("unTotalMinutesPlayed", &GSGameplayStats_t::m_unTotalMinutesPlayed)
 	;
 	py::class_<GSClientGroupStatus_t> _gsclientgroupstatus_t(m, "GSClientGroupStatus");
 	_gsclientgroupstatus_t
 		.def_property_readonly("ptr", [](GSClientGroupStatus_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("SteamIDUser", &GSClientGroupStatus_t::m_SteamIDUser)
 		.def_readwrite("SteamIDGroup", &GSClientGroupStatus_t::m_SteamIDGroup)
 		.def_readwrite("bMember", &GSClientGroupStatus_t::m_bMember)
 		.def_readwrite("bOfficer", &GSClientGroupStatus_t::m_bOfficer)
 	;
 	py::class_<GSReputation_t> _gsreputation_t(m, "GSReputation");
 	_gsreputation_t
 		.def_property_readonly("ptr", [](GSReputation_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("eResult", &GSReputation_t::m_eResult)
 		.def_readwrite("unReputationScore", &GSReputation_t::m_unReputationScore)
 		.def_readwrite("bBanned", &GSReputation_t::m_bBanned)
 		.def_readwrite("unBannedIP", &GSReputation_t::m_unBannedIP)
 		.def_readwrite("usBannedPort", &GSReputation_t::m_usBannedPort)
 		.def_readwrite("ulBannedGameID", &GSReputation_t::m_ulBannedGameID)
 		.def_readwrite("unBanExpires", &GSReputation_t::m_unBanExpires)
 	;
 	py::class_<AssociateWithClanResult_t> _associatewithclanresult_t(m, "AssociateWithClanResult");
 	_associatewithclanresult_t
 		.def_property_readonly("ptr", [](AssociateWithClanResult_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("eResult", &AssociateWithClanResult_t::m_eResult)
 	;
 	py::class_<ComputeNewPlayerCompatibilityResult_t> _computenewplayercompatibilityresult_t(m, "ComputeNewPlayerCompatibilityResult");
 	_computenewplayercompatibilityresult_t
 		.def_property_readonly("ptr", [](ComputeNewPlayerCompatibilityResult_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("eResult", &ComputeNewPlayerCompatibilityResult_t::m_eResult)
 		.def_readwrite("cPlayersThatDontLikeCandidate", &ComputeNewPlayerCompatibilityResult_t::m_cPlayersThatDontLikeCandidate)
 		.def_readwrite("cPlayersThatCandidateDoesntLike", &ComputeNewPlayerCompatibilityResult_t::m_cPlayersThatCandidateDoesntLike)
 		.def_readwrite("cClanPlayersThatDontLikeCandidate", &ComputeNewPlayerCompatibilityResult_t::m_cClanPlayersThatDontLikeCandidate)
 		.def_readwrite("SteamIDCandidate", &ComputeNewPlayerCompatibilityResult_t::m_SteamIDCandidate)
 	;
 	py::class_<GSStatsReceived_t> _gsstatsreceived_t(m, "GSStatsReceived");
 	_gsstatsreceived_t
 		.def_property_readonly("ptr", [](GSStatsReceived_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("eResult", &GSStatsReceived_t::m_eResult)
 		.def_readwrite("steamIDUser", &GSStatsReceived_t::m_steamIDUser)
 	;
 	py::class_<GSStatsStored_t> _gsstatsstored_t(m, "GSStatsStored");
 	_gsstatsstored_t
 		.def_property_readonly("ptr", [](GSStatsStored_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("eResult", &GSStatsStored_t::m_eResult)
 		.def_readwrite("steamIDUser", &GSStatsStored_t::m_steamIDUser)
 	;
 	py::class_<GSStatsUnloaded_t> _gsstatsunloaded_t(m, "GSStatsUnloaded");
 	_gsstatsunloaded_t
 		.def_property_readonly("ptr", [](GSStatsUnloaded_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("steamIDUser", &GSStatsUnloaded_t::m_steamIDUser)
 	;
 	py::class_<SteamNetworkingFakeIPResult_t> _steamnetworkingfakeipresult_t(m, "SteamNetworkingFakeIPResult");
 	_steamnetworkingfakeipresult_t
 		.def_property_readonly("ptr", [](SteamNetworkingFakeIPResult_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("eResult", &SteamNetworkingFakeIPResult_t::m_eResult)
 		.def_readwrite("identity", &SteamNetworkingFakeIPResult_t::m_identity)
 		.def_readwrite("unIP", &SteamNetworkingFakeIPResult_t::m_unIP)
 		.def_property_readonly("unPorts",
			[](SteamNetworkingFakeIPResult_t& x) { return FixedArrayView<uint16, 8>{ x.m_unPorts }; },
			py::return_value_policy::reference_internal)
 	;
 	py::class_<SteamIPAddress_t> _steamipaddress_t(m, "SteamIPAddress");
 	_steamipaddress_t
 		.def_property_readonly("ptr", [](SteamIPAddress_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def("IsSet", &SteamAPI_SteamIPAddress_t_IsSet)
 		.def_property_readonly("rgubIPv6",
			[](SteamIPAddress_t& x) { return FixedArrayView<uint8, 16>{ x.m_rgubIPv6 }; },
			py::return_value_policy::reference_internal)
 		.def_readwrite("eType", &SteamIPAddress_t::m_eType)
 	;
 	py::class_<FriendGameInfo_t> _friendgameinfo_t(m, "FriendGameInfo");
 	_friendgameinfo_t
 		.def_property_readonly("ptr", [](FriendGameInfo_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("gameID", &FriendGameInfo_t::m_gameID)
 		.def_readwrite("unGameIP", &FriendGameInfo_t::m_unGameIP)
 		.def_readwrite("usGamePort", &FriendGameInfo_t::m_usGamePort)
 		.def_readwrite("usQueryPort", &FriendGameInfo_t::m_usQueryPort)
 		.def_readwrite("steamIDLobby", &FriendGameInfo_t::m_steamIDLobby)
 	;
 	py::class_<MatchMakingKeyValuePair_t> _matchmakingkeyvaluepair_t(m, "MatchMakingKeyValuePair");
 	_matchmakingkeyvaluepair_t
 		.def_property_readonly("ptr", [](MatchMakingKeyValuePair_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_property("szKey",
			[](const MatchMakingKeyValuePair_t& x) { return get_char_array(x.m_szKey); },
			[](MatchMakingKeyValuePair_t& x, const std::string& s) { set_char_array(x.m_szKey, s); })
 		.def_property("szValue",
			[](const MatchMakingKeyValuePair_t& x) { return get_char_array(x.m_szValue); },
			[](MatchMakingKeyValuePair_t& x, const std::string& s) { set_char_array(x.m_szValue, s); })
 	;
 	py::class_<servernetadr_t> _servernetadr_t(m, "servernetadr");
 	_servernetadr_t
 		.def_property_readonly("ptr", [](servernetadr_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def("Init", &SteamAPI_servernetadr_t_Init, py::arg("ip"), py::arg("usQueryPort"), py::arg("usConnectionPort"))
 		.def("GetQueryPort", &SteamAPI_servernetadr_t_GetQueryPort)
 		.def("SetQueryPort", &SteamAPI_servernetadr_t_SetQueryPort, py::arg("usPort"))
 		.def("GetConnectionPort", &SteamAPI_servernetadr_t_GetConnectionPort)
 		.def("SetConnectionPort", &SteamAPI_servernetadr_t_SetConnectionPort, py::arg("usPort"))
 		.def("GetIP", &SteamAPI_servernetadr_t_GetIP)
 		.def("SetIP", &SteamAPI_servernetadr_t_SetIP, py::arg("unIP"))
 		.def("GetConnectionAddressString", &SteamAPI_servernetadr_t_GetConnectionAddressString)
 		.def("GetQueryAddressString", &SteamAPI_servernetadr_t_GetQueryAddressString)
 		.def("__lt__", &SteamAPI_servernetadr_t_IsLessThan, py::arg("netadr"))
 		.def("operator=", &SteamAPI_servernetadr_t_Assign, py::arg("that"))
 	;
 	py::class_<gameserveritem_t> _gameserveritem_t(m, "gameserveritem");
 	_gameserveritem_t
 		.def_property_readonly("ptr", [](gameserveritem_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def("GetName", &SteamAPI_gameserveritem_t_GetName)
 		.def("SetName", [](gameserveritem_t* self, uintptr_t pName) { 
			const char * ptr0 = reinterpret_cast<const char *>(pName);
			SteamAPI_gameserveritem_t_SetName(self, ptr0); }, py::arg("pName"))
 		.def_readwrite("NetAdr", &gameserveritem_t::m_NetAdr)
 		.def_readwrite("nPing", &gameserveritem_t::m_nPing)
 		.def_readwrite("bHadSuccessfulResponse", &gameserveritem_t::m_bHadSuccessfulResponse)
 		.def_readwrite("bDoNotRefresh", &gameserveritem_t::m_bDoNotRefresh)
 		.def_property("szGameDir",
			[](const gameserveritem_t& x) { return get_char_array(x.m_szGameDir); },
			[](gameserveritem_t& x, const std::string& s) { set_char_array(x.m_szGameDir, s); })
 		.def_property("szMap",
			[](const gameserveritem_t& x) { return get_char_array(x.m_szMap); },
			[](gameserveritem_t& x, const std::string& s) { set_char_array(x.m_szMap, s); })
 		.def_property("szGameDescription",
			[](const gameserveritem_t& x) { return get_char_array(x.m_szGameDescription); },
			[](gameserveritem_t& x, const std::string& s) { set_char_array(x.m_szGameDescription, s); })
 		.def_readwrite("nAppID", &gameserveritem_t::m_nAppID)
 		.def_readwrite("nPlayers", &gameserveritem_t::m_nPlayers)
 		.def_readwrite("nMaxPlayers", &gameserveritem_t::m_nMaxPlayers)
 		.def_readwrite("nBotPlayers", &gameserveritem_t::m_nBotPlayers)
 		.def_readwrite("bPassword", &gameserveritem_t::m_bPassword)
 		.def_readwrite("bSecure", &gameserveritem_t::m_bSecure)
 		.def_readwrite("ulTimeLastPlayed", &gameserveritem_t::m_ulTimeLastPlayed)
 		.def_readwrite("nServerVersion", &gameserveritem_t::m_nServerVersion)
 		.def_property("szGameTags",
			[](const gameserveritem_t& x) { return get_char_array(x.m_szGameTags); },
			[](gameserveritem_t& x, const std::string& s) { set_char_array(x.m_szGameTags, s); })
 		.def_readwrite("steamID", &gameserveritem_t::m_steamID)
 	;
 	py::class_<SteamPartyBeaconLocation_t> _steampartybeaconlocation_t(m, "SteamPartyBeaconLocation");
 	_steampartybeaconlocation_t
 		.def_property_readonly("ptr", [](SteamPartyBeaconLocation_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("eType", &SteamPartyBeaconLocation_t::m_eType)
 		.def_readwrite("ulLocationID", &SteamPartyBeaconLocation_t::m_ulLocationID)
 	;
 	py::class_<SteamParamStringArray_t> _steamparamstringarray_t(m, "SteamParamStringArray");
 	_steamparamstringarray_t
 		.def_property_readonly("ptr", [](SteamParamStringArray_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("nNumStrings", &SteamParamStringArray_t::m_nNumStrings)
 	;
 	py::class_<LeaderboardEntry_t> _leaderboardentry_t(m, "LeaderboardEntry");
 	_leaderboardentry_t
 		.def_property_readonly("ptr", [](LeaderboardEntry_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("steamIDUser", &LeaderboardEntry_t::m_steamIDUser)
 		.def_readwrite("nGlobalRank", &LeaderboardEntry_t::m_nGlobalRank)
 		.def_readwrite("nScore", &LeaderboardEntry_t::m_nScore)
 		.def_readwrite("cDetails", &LeaderboardEntry_t::m_cDetails)
 		.def_readwrite("hUGC", &LeaderboardEntry_t::m_hUGC)
 	;
 	py::class_<P2PSessionState_t> _p2psessionstate_t(m, "P2PSessionState");
 	_p2psessionstate_t
 		.def_property_readonly("ptr", [](P2PSessionState_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("bConnectionActive", &P2PSessionState_t::m_bConnectionActive)
 		.def_readwrite("bConnecting", &P2PSessionState_t::m_bConnecting)
 		.def_readwrite("eP2PSessionError", &P2PSessionState_t::m_eP2PSessionError)
 		.def_readwrite("bUsingRelay", &P2PSessionState_t::m_bUsingRelay)
 		.def_readwrite("nBytesQueuedForSend", &P2PSessionState_t::m_nBytesQueuedForSend)
 		.def_readwrite("nPacketsQueuedForSend", &P2PSessionState_t::m_nPacketsQueuedForSend)
 		.def_readwrite("nRemoteIP", &P2PSessionState_t::m_nRemoteIP)
 		.def_readwrite("nRemotePort", &P2PSessionState_t::m_nRemotePort)
 	;
 	py::class_<InputAnalogActionData_t> _inputanalogactiondata_t(m, "InputAnalogActionData");
 	_inputanalogactiondata_t
 		.def_property_readonly("ptr", [](InputAnalogActionData_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("eMode", &InputAnalogActionData_t::eMode)
 		.def_readwrite("x", &InputAnalogActionData_t::x)
 		.def_readwrite("y", &InputAnalogActionData_t::y)
 		.def_readwrite("bActive", &InputAnalogActionData_t::bActive)
 	;
 	py::class_<InputDigitalActionData_t> _inputdigitalactiondata_t(m, "InputDigitalActionData");
 	_inputdigitalactiondata_t
 		.def_property_readonly("ptr", [](InputDigitalActionData_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("bState", &InputDigitalActionData_t::bState)
 		.def_readwrite("bActive", &InputDigitalActionData_t::bActive)
 	;
 	py::class_<InputMotionData_t> _inputmotiondata_t(m, "InputMotionData");
 	_inputmotiondata_t
 		.def_property_readonly("ptr", [](InputMotionData_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("rotQuatX", &InputMotionData_t::rotQuatX)
 		.def_readwrite("rotQuatY", &InputMotionData_t::rotQuatY)
 		.def_readwrite("rotQuatZ", &InputMotionData_t::rotQuatZ)
 		.def_readwrite("rotQuatW", &InputMotionData_t::rotQuatW)
 		.def_readwrite("posAccelX", &InputMotionData_t::posAccelX)
 		.def_readwrite("posAccelY", &InputMotionData_t::posAccelY)
 		.def_readwrite("posAccelZ", &InputMotionData_t::posAccelZ)
 		.def_readwrite("rotVelX", &InputMotionData_t::rotVelX)
 		.def_readwrite("rotVelY", &InputMotionData_t::rotVelY)
 		.def_readwrite("rotVelZ", &InputMotionData_t::rotVelZ)
 	;
 	py::class_<SteamInputActionEvent_t> _steaminputactionevent_t(m, "SteamInputActionEvent");
 	_steaminputactionevent_t
 		.def_property_readonly("ptr", [](SteamInputActionEvent_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("controllerHandle", &SteamInputActionEvent_t::controllerHandle)
 		.def_readwrite("eEventType", &SteamInputActionEvent_t::eEventType)
 		.def_readwrite("analogAction", &SteamInputActionEvent_t::analogAction)
 	;
 	py::class_<SteamUGCDetails_t> _steamugcdetails_t(m, "SteamUGCDetails");
 	_steamugcdetails_t
 		.def_property_readonly("ptr", [](SteamUGCDetails_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("nPublishedFileId", &SteamUGCDetails_t::m_nPublishedFileId)
 		.def_readwrite("eResult", &SteamUGCDetails_t::m_eResult)
 		.def_readwrite("eFileType", &SteamUGCDetails_t::m_eFileType)
 		.def_readwrite("nCreatorAppID", &SteamUGCDetails_t::m_nCreatorAppID)
 		.def_readwrite("nConsumerAppID", &SteamUGCDetails_t::m_nConsumerAppID)
 		.def_property("rgchTitle",
			[](const SteamUGCDetails_t& x) { return get_char_array(x.m_rgchTitle); },
			[](SteamUGCDetails_t& x, const std::string& s) { set_char_array(x.m_rgchTitle, s); })
 		.def_property("rgchDescription",
			[](const SteamUGCDetails_t& x) { return get_char_array(x.m_rgchDescription); },
			[](SteamUGCDetails_t& x, const std::string& s) { set_char_array(x.m_rgchDescription, s); })
 		.def_readwrite("ulSteamIDOwner", &SteamUGCDetails_t::m_ulSteamIDOwner)
 		.def_readwrite("rtimeCreated", &SteamUGCDetails_t::m_rtimeCreated)
 		.def_readwrite("rtimeUpdated", &SteamUGCDetails_t::m_rtimeUpdated)
 		.def_readwrite("rtimeAddedToUserList", &SteamUGCDetails_t::m_rtimeAddedToUserList)
 		.def_readwrite("eVisibility", &SteamUGCDetails_t::m_eVisibility)
 		.def_readwrite("bBanned", &SteamUGCDetails_t::m_bBanned)
 		.def_readwrite("bAcceptedForUse", &SteamUGCDetails_t::m_bAcceptedForUse)
 		.def_readwrite("bTagsTruncated", &SteamUGCDetails_t::m_bTagsTruncated)
 		.def_property("rgchTags",
			[](const SteamUGCDetails_t& x) { return get_char_array(x.m_rgchTags); },
			[](SteamUGCDetails_t& x, const std::string& s) { set_char_array(x.m_rgchTags, s); })
 		.def_readwrite("hFile", &SteamUGCDetails_t::m_hFile)
 		.def_readwrite("hPreviewFile", &SteamUGCDetails_t::m_hPreviewFile)
 		.def_property("pchFileName",
			[](const SteamUGCDetails_t& x) { return get_char_array(x.m_pchFileName); },
			[](SteamUGCDetails_t& x, const std::string& s) { set_char_array(x.m_pchFileName, s); })
 		.def_readwrite("nFileSize", &SteamUGCDetails_t::m_nFileSize)
 		.def_readwrite("nPreviewFileSize", &SteamUGCDetails_t::m_nPreviewFileSize)
 		.def_property("rgchURL",
			[](const SteamUGCDetails_t& x) { return get_char_array(x.m_rgchURL); },
			[](SteamUGCDetails_t& x, const std::string& s) { set_char_array(x.m_rgchURL, s); })
 		.def_readwrite("unVotesUp", &SteamUGCDetails_t::m_unVotesUp)
 		.def_readwrite("unVotesDown", &SteamUGCDetails_t::m_unVotesDown)
 		.def_readwrite("flScore", &SteamUGCDetails_t::m_flScore)
 		.def_readwrite("unNumChildren", &SteamUGCDetails_t::m_unNumChildren)
 		.def_readwrite("ulTotalFilesSize", &SteamUGCDetails_t::m_ulTotalFilesSize)
 	;
 	py::class_<SteamItemDetails_t> _steamitemdetails_t(m, "SteamItemDetails");
 	_steamitemdetails_t
 		.def_property_readonly("ptr", [](SteamItemDetails_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("itemId", &SteamItemDetails_t::m_itemId)
 		.def_readwrite("iDefinition", &SteamItemDetails_t::m_iDefinition)
 		.def_readwrite("unQuantity", &SteamItemDetails_t::m_unQuantity)
 		.def_readwrite("unFlags", &SteamItemDetails_t::m_unFlags)
 	;
 	py::class_<RemotePlayInputMouseMotion_t> _remoteplayinputmousemotion_t(m, "RemotePlayInputMouseMotion");
 	_remoteplayinputmousemotion_t
 		.def_property_readonly("ptr", [](RemotePlayInputMouseMotion_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("bAbsolute", &RemotePlayInputMouseMotion_t::m_bAbsolute)
 		.def_readwrite("flNormalizedX", &RemotePlayInputMouseMotion_t::m_flNormalizedX)
 		.def_readwrite("flNormalizedY", &RemotePlayInputMouseMotion_t::m_flNormalizedY)
 		.def_readwrite("nDeltaX", &RemotePlayInputMouseMotion_t::m_nDeltaX)
 		.def_readwrite("nDeltaY", &RemotePlayInputMouseMotion_t::m_nDeltaY)
 	;
 	py::class_<RemotePlayInputMouseWheel_t> _remoteplayinputmousewheel_t(m, "RemotePlayInputMouseWheel");
 	_remoteplayinputmousewheel_t
 		.def_property_readonly("ptr", [](RemotePlayInputMouseWheel_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("eDirection", &RemotePlayInputMouseWheel_t::m_eDirection)
 		.def_readwrite("flAmount", &RemotePlayInputMouseWheel_t::m_flAmount)
 	;
 	py::class_<RemotePlayInputKey_t> _remoteplayinputkey_t(m, "RemotePlayInputKey");
 	_remoteplayinputkey_t
 		.def_property_readonly("ptr", [](RemotePlayInputKey_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("eScancode", &RemotePlayInputKey_t::m_eScancode)
 		.def_readwrite("unModifiers", &RemotePlayInputKey_t::m_unModifiers)
 		.def_readwrite("unKeycode", &RemotePlayInputKey_t::m_unKeycode)
 	;
 	py::class_<RemotePlayInput_t> _remoteplayinput_t(m, "RemotePlayInput");
 	_remoteplayinput_t
 		.def_property_readonly("ptr", [](RemotePlayInput_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("unSessionID", &RemotePlayInput_t::m_unSessionID)
 		.def_readwrite("eType", &RemotePlayInput_t::m_eType)
 		.def_property("padding",
			[](const RemotePlayInput_t& x) { return get_char_array(x.padding); },
			[](RemotePlayInput_t& x, const std::string& s) { set_char_array(x.padding, s); })
 	;
 	py::class_<SteamNetworkingIPAddr> _steamnetworkingipaddr(m, "SteamNetworkingIPAddr");
 	_steamnetworkingipaddr
 		.def_property_readonly("ptr", [](SteamNetworkingIPAddr& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def("Clear", &SteamAPI_SteamNetworkingIPAddr_Clear)
 		.def("IsIPv6AllZeros", &SteamAPI_SteamNetworkingIPAddr_IsIPv6AllZeros)
 		.def("SetIPv6", [](SteamNetworkingIPAddr* self, uintptr_t ipv6, uint16 nPort) { 
			const uint8 * ptr0 = reinterpret_cast<const uint8 *>(ipv6);
			SteamAPI_SteamNetworkingIPAddr_SetIPv6(self, ptr0, nPort); }, py::arg("ipv6"), py::arg("nPort"))
 		.def("SetIPv4", &SteamAPI_SteamNetworkingIPAddr_SetIPv4, py::arg("nIP"), py::arg("nPort"))
 		.def("IsIPv4", &SteamAPI_SteamNetworkingIPAddr_IsIPv4)
 		.def("GetIPv4", &SteamAPI_SteamNetworkingIPAddr_GetIPv4)
 		.def("SetIPv6LocalHost", &SteamAPI_SteamNetworkingIPAddr_SetIPv6LocalHost, py::arg("nPort"))
 		.def("IsLocalHost", &SteamAPI_SteamNetworkingIPAddr_IsLocalHost)
 		.def("ToString", [](SteamNetworkingIPAddr* self, uintptr_t buf, uint32 cbBuf, bool bWithPort) { 
			char * ptr0 = reinterpret_cast<char *>(buf);
			SteamAPI_SteamNetworkingIPAddr_ToString(self, ptr0, cbBuf, bWithPort); }, py::arg("buf"), py::arg("cbBuf"), py::arg("bWithPort"))
 		.def("ParseString", [](SteamNetworkingIPAddr* self, uintptr_t pszStr) { 
			const char * ptr0 = reinterpret_cast<const char *>(pszStr);
			return (bool)SteamAPI_SteamNetworkingIPAddr_ParseString(self, ptr0); }, py::arg("pszStr"))
 		.def("__eq__", &SteamAPI_SteamNetworkingIPAddr_IsEqualTo, py::arg("x"))
 		.def("GetFakeIPType", &SteamAPI_SteamNetworkingIPAddr_GetFakeIPType)
 		.def("IsFakeIP", &SteamAPI_SteamNetworkingIPAddr_IsFakeIP)
 		.def_property_readonly("ipv6",
			[](SteamNetworkingIPAddr& x) { return FixedArrayView<uint8, 16>{ x.m_ipv6 }; },
			py::return_value_policy::reference_internal)
 		.def_readwrite("port", &SteamNetworkingIPAddr::m_port)
 	;
 	py::class_<SteamNetworkingIdentity> _steamnetworkingidentity(m, "SteamNetworkingIdentity");
 	_steamnetworkingidentity
 		.def_property_readonly("ptr", [](SteamNetworkingIdentity& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def("Clear", &SteamAPI_SteamNetworkingIdentity_Clear)
 		.def("IsInvalid", &SteamAPI_SteamNetworkingIdentity_IsInvalid)
 		.def("SetSteamID", &SteamAPI_SteamNetworkingIdentity_SetSteamID, py::arg("steamID"))
 		.def("GetSteamID", &SteamAPI_SteamNetworkingIdentity_GetSteamID)
 		.def("SetSteamID64", &SteamAPI_SteamNetworkingIdentity_SetSteamID64, py::arg("steamID"))
 		.def("GetSteamID64", &SteamAPI_SteamNetworkingIdentity_GetSteamID64)
 		.def("SetXboxPairwiseID", [](SteamNetworkingIdentity* self, uintptr_t pszString) { 
			const char * ptr0 = reinterpret_cast<const char *>(pszString);
			return (bool)SteamAPI_SteamNetworkingIdentity_SetXboxPairwiseID(self, ptr0); }, py::arg("pszString"))
 		.def("GetXboxPairwiseID", &SteamAPI_SteamNetworkingIdentity_GetXboxPairwiseID)
 		.def("SetPSNID", &SteamAPI_SteamNetworkingIdentity_SetPSNID, py::arg("id"))
 		.def("GetPSNID", &SteamAPI_SteamNetworkingIdentity_GetPSNID)
 		.def("SetIPAddr", &SteamAPI_SteamNetworkingIdentity_SetIPAddr, py::arg("addr"))
 		.def("GetIPAddr", &SteamAPI_SteamNetworkingIdentity_GetIPAddr)
 		.def("SetIPv4Addr", &SteamAPI_SteamNetworkingIdentity_SetIPv4Addr, py::arg("nIPv4"), py::arg("nPort"))
 		.def("GetIPv4", &SteamAPI_SteamNetworkingIdentity_GetIPv4)
 		.def("GetFakeIPType", &SteamAPI_SteamNetworkingIdentity_GetFakeIPType)
 		.def("IsFakeIP", &SteamAPI_SteamNetworkingIdentity_IsFakeIP)
 		.def("SetLocalHost", &SteamAPI_SteamNetworkingIdentity_SetLocalHost)
 		.def("IsLocalHost", &SteamAPI_SteamNetworkingIdentity_IsLocalHost)
 		.def("SetGenericString", [](SteamNetworkingIdentity* self, uintptr_t pszString) { 
			const char * ptr0 = reinterpret_cast<const char *>(pszString);
			return (bool)SteamAPI_SteamNetworkingIdentity_SetGenericString(self, ptr0); }, py::arg("pszString"))
 		.def("GetGenericString", &SteamAPI_SteamNetworkingIdentity_GetGenericString)
 		.def("SetGenericBytes", [](SteamNetworkingIdentity* self, uintptr_t data, uint32 cbLen) { 
			const void * ptr0 = reinterpret_cast<const void *>(data);
			return (bool)SteamAPI_SteamNetworkingIdentity_SetGenericBytes(self, ptr0, cbLen); }, py::arg("data"), py::arg("cbLen"))
 		.def("GetGenericBytes", [](SteamNetworkingIdentity* self, uintptr_t cbLen) { 
			int & ptr0 = *reinterpret_cast<int *>(cbLen);
			return (const uint8 *)SteamAPI_SteamNetworkingIdentity_GetGenericBytes(self, ptr0); }, py::arg("cbLen"))
 		.def("__eq__", &SteamAPI_SteamNetworkingIdentity_IsEqualTo, py::arg("x"))
 		.def("ToString", [](SteamNetworkingIdentity* self, uintptr_t buf, uint32 cbBuf) { 
			char * ptr0 = reinterpret_cast<char *>(buf);
			SteamAPI_SteamNetworkingIdentity_ToString(self, ptr0, cbBuf); }, py::arg("buf"), py::arg("cbBuf"))
 		.def("ParseString", [](SteamNetworkingIdentity* self, uintptr_t pszStr) { 
			const char * ptr0 = reinterpret_cast<const char *>(pszStr);
			return (bool)SteamAPI_SteamNetworkingIdentity_ParseString(self, ptr0); }, py::arg("pszStr"))
 		.def_readwrite("eType", &SteamNetworkingIdentity::m_eType)
 		.def_readwrite("cbSize", &SteamNetworkingIdentity::m_cbSize)
 		.def_property("szUnknownRawString",
			[](const SteamNetworkingIdentity& x) { return get_char_array(x.m_szUnknownRawString); },
			[](SteamNetworkingIdentity& x, const std::string& s) { set_char_array(x.m_szUnknownRawString, s); })
 	;
 	py::class_<SteamNetConnectionInfo_t> _steamnetconnectioninfo_t(m, "SteamNetConnectionInfo");
 	_steamnetconnectioninfo_t
 		.def_property_readonly("ptr", [](SteamNetConnectionInfo_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("identityRemote", &SteamNetConnectionInfo_t::m_identityRemote)
 		.def_readwrite("nUserData", &SteamNetConnectionInfo_t::m_nUserData)
 		.def_readwrite("hListenSocket", &SteamNetConnectionInfo_t::m_hListenSocket)
 		.def_readwrite("addrRemote", &SteamNetConnectionInfo_t::m_addrRemote)
 		.def_readwrite("_pad1", &SteamNetConnectionInfo_t::m__pad1)
 		.def_readwrite("idPOPRemote", &SteamNetConnectionInfo_t::m_idPOPRemote)
 		.def_readwrite("idPOPRelay", &SteamNetConnectionInfo_t::m_idPOPRelay)
 		.def_readwrite("eState", &SteamNetConnectionInfo_t::m_eState)
 		.def_readwrite("eEndReason", &SteamNetConnectionInfo_t::m_eEndReason)
 		.def_property("szEndDebug",
			[](const SteamNetConnectionInfo_t& x) { return get_char_array(x.m_szEndDebug); },
			[](SteamNetConnectionInfo_t& x, const std::string& s) { set_char_array(x.m_szEndDebug, s); })
 		.def_property("szConnectionDescription",
			[](const SteamNetConnectionInfo_t& x) { return get_char_array(x.m_szConnectionDescription); },
			[](SteamNetConnectionInfo_t& x, const std::string& s) { set_char_array(x.m_szConnectionDescription, s); })
 		.def_readwrite("nFlags", &SteamNetConnectionInfo_t::m_nFlags)
 		.def_property_readonly("reserved",
			[](SteamNetConnectionInfo_t& x) { return FixedArrayView<uint32, 63>{ x.reserved }; },
			py::return_value_policy::reference_internal)
 	;
 	py::class_<SteamNetConnectionRealTimeStatus_t> _steamnetconnectionrealtimestatus_t(m, "SteamNetConnectionRealTimeStatus");
 	_steamnetconnectionrealtimestatus_t
 		.def_property_readonly("ptr", [](SteamNetConnectionRealTimeStatus_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("eState", &SteamNetConnectionRealTimeStatus_t::m_eState)
 		.def_readwrite("nPing", &SteamNetConnectionRealTimeStatus_t::m_nPing)
 		.def_readwrite("flConnectionQualityLocal", &SteamNetConnectionRealTimeStatus_t::m_flConnectionQualityLocal)
 		.def_readwrite("flConnectionQualityRemote", &SteamNetConnectionRealTimeStatus_t::m_flConnectionQualityRemote)
 		.def_readwrite("flOutPacketsPerSec", &SteamNetConnectionRealTimeStatus_t::m_flOutPacketsPerSec)
 		.def_readwrite("flOutBytesPerSec", &SteamNetConnectionRealTimeStatus_t::m_flOutBytesPerSec)
 		.def_readwrite("flInPacketsPerSec", &SteamNetConnectionRealTimeStatus_t::m_flInPacketsPerSec)
 		.def_readwrite("flInBytesPerSec", &SteamNetConnectionRealTimeStatus_t::m_flInBytesPerSec)
 		.def_readwrite("nSendRateBytesPerSecond", &SteamNetConnectionRealTimeStatus_t::m_nSendRateBytesPerSecond)
 		.def_readwrite("cbPendingUnreliable", &SteamNetConnectionRealTimeStatus_t::m_cbPendingUnreliable)
 		.def_readwrite("cbPendingReliable", &SteamNetConnectionRealTimeStatus_t::m_cbPendingReliable)
 		.def_readwrite("cbSentUnackedReliable", &SteamNetConnectionRealTimeStatus_t::m_cbSentUnackedReliable)
 		.def_readwrite("usecQueueTime", &SteamNetConnectionRealTimeStatus_t::m_usecQueueTime)
 		.def_readwrite("usecMaxJitter", &SteamNetConnectionRealTimeStatus_t::m_usecMaxJitter)
 		.def_property_readonly("reserved",
			[](SteamNetConnectionRealTimeStatus_t& x) { return FixedArrayView<uint32, 15>{ x.reserved }; },
			py::return_value_policy::reference_internal)
 	;
 	py::class_<SteamNetConnectionRealTimeLaneStatus_t> _steamnetconnectionrealtimelanestatus_t(m, "SteamNetConnectionRealTimeLaneStatus");
 	_steamnetconnectionrealtimelanestatus_t
 		.def_property_readonly("ptr", [](SteamNetConnectionRealTimeLaneStatus_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("cbPendingUnreliable", &SteamNetConnectionRealTimeLaneStatus_t::m_cbPendingUnreliable)
 		.def_readwrite("cbPendingReliable", &SteamNetConnectionRealTimeLaneStatus_t::m_cbPendingReliable)
 		.def_readwrite("cbSentUnackedReliable", &SteamNetConnectionRealTimeLaneStatus_t::m_cbSentUnackedReliable)
 		.def_readwrite("_reservePad1", &SteamNetConnectionRealTimeLaneStatus_t::_reservePad1)
 		.def_readwrite("usecQueueTime", &SteamNetConnectionRealTimeLaneStatus_t::m_usecQueueTime)
 		.def_property_readonly("reserved",
			[](SteamNetConnectionRealTimeLaneStatus_t& x) { return FixedArrayView<uint32, 10>{ x.reserved }; },
			py::return_value_policy::reference_internal)
 	;
 	py::class_<SteamNetworkingMessage_t, ReleasePtr<SteamNetworkingMessage_t>> _steamnetworkingmessage_t(m, "SteamNetworkingMessage");
 	_steamnetworkingmessage_t
 		.def_property_readonly("ptr", [](SteamNetworkingMessage_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def_readwrite("pData", &SteamNetworkingMessage_t::m_pData)
 		.def_readwrite("cbSize", &SteamNetworkingMessage_t::m_cbSize)
 		.def_readwrite("conn", &SteamNetworkingMessage_t::m_conn)
 		.def_readwrite("identityPeer", &SteamNetworkingMessage_t::m_identityPeer)
 		.def_readwrite("nConnUserData", &SteamNetworkingMessage_t::m_nConnUserData)
 		.def_readwrite("usecTimeReceived", &SteamNetworkingMessage_t::m_usecTimeReceived)
 		.def_readwrite("nMessageNumber", &SteamNetworkingMessage_t::m_nMessageNumber)
 		.def_readwrite("nChannel", &SteamNetworkingMessage_t::m_nChannel)
 		.def_readwrite("nFlags", &SteamNetworkingMessage_t::m_nFlags)
 		.def_readwrite("nUserData", &SteamNetworkingMessage_t::m_nUserData)
 		.def_readwrite("idxLane", &SteamNetworkingMessage_t::m_idxLane)
 		.def_readwrite("_pad1__", &SteamNetworkingMessage_t::_pad1__)
 	;
 	py::class_<SteamNetworkPingLocation_t> _steamnetworkpinglocation_t(m, "SteamNetworkPingLocation");
 	_steamnetworkpinglocation_t
 		.def_property_readonly("ptr", [](SteamNetworkPingLocation_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_property_readonly("data",
			[](SteamNetworkPingLocation_t& x) { return FixedArrayView<uint8, 512>{ x.m_data }; },
			py::return_value_policy::reference_internal)
 	;
 	py::class_<SteamNetworkingConfigValue_t> _steamnetworkingconfigvalue_t(m, "SteamNetworkingConfigValue");
 	_steamnetworkingconfigvalue_t
 		.def_property_readonly("ptr", [](SteamNetworkingConfigValue_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def("SetInt32", &SteamAPI_SteamNetworkingConfigValue_t_SetInt32, py::arg("eVal"), py::arg("data"))
 		.def("SetInt64", &SteamAPI_SteamNetworkingConfigValue_t_SetInt64, py::arg("eVal"), py::arg("data"))
 		.def("SetFloat", &SteamAPI_SteamNetworkingConfigValue_t_SetFloat, py::arg("eVal"), py::arg("data"))
 		.def("SetPtr", [](SteamNetworkingConfigValue_t* self, ESteamNetworkingConfigValue eVal, uintptr_t data) { 
			void * ptr1 = reinterpret_cast<void *>(data);
			SteamAPI_SteamNetworkingConfigValue_t_SetPtr(self, eVal, ptr1); }, py::arg("eVal"), py::arg("data"))
 		.def("SetString", [](SteamNetworkingConfigValue_t* self, ESteamNetworkingConfigValue eVal, uintptr_t data) { 
			const char * ptr1 = reinterpret_cast<const char *>(data);
			SteamAPI_SteamNetworkingConfigValue_t_SetString(self, eVal, ptr1); }, py::arg("eVal"), py::arg("data"))
 		.def_readwrite("eValue", &SteamNetworkingConfigValue_t::m_eValue)
 		.def_readwrite("eDataType", &SteamNetworkingConfigValue_t::m_eDataType)
 		.def_readwrite("val", &SteamNetworkingConfigValue_t::m_val)
 	;
 	py::class_<SteamDatagramHostedAddress> _steamdatagramhostedaddress(m, "SteamDatagramHostedAddress");
 	_steamdatagramhostedaddress
 		.def_property_readonly("ptr", [](SteamDatagramHostedAddress& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def("Clear", &SteamAPI_SteamDatagramHostedAddress_Clear)
 		.def("GetPopID", &SteamAPI_SteamDatagramHostedAddress_GetPopID)
 		.def("SetDevAddress", &SteamAPI_SteamDatagramHostedAddress_SetDevAddress, py::arg("nIP"), py::arg("nPort"), py::arg("popid"))
 		.def_readwrite("cbSize", &SteamDatagramHostedAddress::m_cbSize)
 		.def_property("data",
			[](const SteamDatagramHostedAddress& x) { return get_char_array(x.m_data); },
			[](SteamDatagramHostedAddress& x, const std::string& s) { set_char_array(x.m_data, s); })
 	;
 	py::class_<SteamDatagramGameCoordinatorServerLogin> _steamdatagramgamecoordinatorserverlogin(m, "SteamDatagramGameCoordinatorServerLogin");
 	_steamdatagramgamecoordinatorserverlogin
 		.def_property_readonly("ptr", [](SteamDatagramGameCoordinatorServerLogin& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("identity", &SteamDatagramGameCoordinatorServerLogin::m_identity)
 		.def_readwrite("routing", &SteamDatagramGameCoordinatorServerLogin::m_routing)
 		.def_readwrite("nAppID", &SteamDatagramGameCoordinatorServerLogin::m_nAppID)
 		.def_readwrite("rtime", &SteamDatagramGameCoordinatorServerLogin::m_rtime)
 		.def_readwrite("cbAppData", &SteamDatagramGameCoordinatorServerLogin::m_cbAppData)
 		.def_property("appData",
			[](const SteamDatagramGameCoordinatorServerLogin& x) { return get_char_array(x.m_appData); },
			[](SteamDatagramGameCoordinatorServerLogin& x, const std::string& s) { set_char_array(x.m_appData, s); })
 	;
 	m.def("SteamClient",[]() -> ISteamClient* { return SteamClient(); }, py::return_value_policy::reference);
 	py::class_<ISteamClient, std::unique_ptr<ISteamClient, py::nodelete>> _isteamclient(m, "ISteamClient");
 	_isteamclient
 		.def_property_readonly("ptr", [](ISteamClient& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def("CreateSteamPipe", &SteamAPI_ISteamClient_CreateSteamPipe)
 		.def("BReleaseSteamPipe", &SteamAPI_ISteamClient_BReleaseSteamPipe, py::arg("hSteamPipe"))
 		.def("ConnectToGlobalUser", &SteamAPI_ISteamClient_ConnectToGlobalUser, py::arg("hSteamPipe"))
 		.def("CreateLocalUser", [](ISteamClient* self, uintptr_t phSteamPipe, EAccountType eAccountType) { 
			HSteamPipe * ptr0 = reinterpret_cast<HSteamPipe *>(phSteamPipe);
			return (HSteamUser)SteamAPI_ISteamClient_CreateLocalUser(self, ptr0, eAccountType); }, py::arg("phSteamPipe"), py::arg("eAccountType"))
 		.def("ReleaseUser", &SteamAPI_ISteamClient_ReleaseUser, py::arg("hSteamPipe"), py::arg("hUser"))
 		.def("GetISteamUser", [](ISteamClient* self, HSteamUser hSteamUser, HSteamPipe hSteamPipe, uintptr_t pchVersion) { 
			const char * ptr2 = reinterpret_cast<const char *>(pchVersion);
			return (ISteamUser *)SteamAPI_ISteamClient_GetISteamUser(self, hSteamUser, hSteamPipe, ptr2); }, py::arg("hSteamUser"), py::arg("hSteamPipe"), py::arg("pchVersion"))
 		.def("GetISteamGameServer", [](ISteamClient* self, HSteamUser hSteamUser, HSteamPipe hSteamPipe, uintptr_t pchVersion) { 
			const char * ptr2 = reinterpret_cast<const char *>(pchVersion);
			return (ISteamGameServer *)SteamAPI_ISteamClient_GetISteamGameServer(self, hSteamUser, hSteamPipe, ptr2); }, py::arg("hSteamUser"), py::arg("hSteamPipe"), py::arg("pchVersion"))
 		.def("SetLocalIPBinding", &SteamAPI_ISteamClient_SetLocalIPBinding, py::arg("unIP"), py::arg("usPort"))
 		.def("GetISteamFriends", [](ISteamClient* self, HSteamUser hSteamUser, HSteamPipe hSteamPipe, uintptr_t pchVersion) { 
			const char * ptr2 = reinterpret_cast<const char *>(pchVersion);
			return (ISteamFriends *)SteamAPI_ISteamClient_GetISteamFriends(self, hSteamUser, hSteamPipe, ptr2); }, py::arg("hSteamUser"), py::arg("hSteamPipe"), py::arg("pchVersion"))
 		.def("GetISteamUtils", [](ISteamClient* self, HSteamPipe hSteamPipe, uintptr_t pchVersion) { 
			const char * ptr1 = reinterpret_cast<const char *>(pchVersion);
			return (ISteamUtils *)SteamAPI_ISteamClient_GetISteamUtils(self, hSteamPipe, ptr1); }, py::arg("hSteamPipe"), py::arg("pchVersion"))
 		.def("GetISteamMatchmaking", [](ISteamClient* self, HSteamUser hSteamUser, HSteamPipe hSteamPipe, uintptr_t pchVersion) { 
			const char * ptr2 = reinterpret_cast<const char *>(pchVersion);
			return (ISteamMatchmaking *)SteamAPI_ISteamClient_GetISteamMatchmaking(self, hSteamUser, hSteamPipe, ptr2); }, py::arg("hSteamUser"), py::arg("hSteamPipe"), py::arg("pchVersion"))
 		.def("GetISteamMatchmakingServers", [](ISteamClient* self, HSteamUser hSteamUser, HSteamPipe hSteamPipe, uintptr_t pchVersion) { 
			const char * ptr2 = reinterpret_cast<const char *>(pchVersion);
			return (ISteamMatchmakingServers *)SteamAPI_ISteamClient_GetISteamMatchmakingServers(self, hSteamUser, hSteamPipe, ptr2); }, py::arg("hSteamUser"), py::arg("hSteamPipe"), py::arg("pchVersion"))
 		.def("GetISteamGenericInterface", [](ISteamClient* self, HSteamUser hSteamUser, HSteamPipe hSteamPipe, uintptr_t pchVersion) { 
			const char * ptr2 = reinterpret_cast<const char *>(pchVersion);
			return (void *)SteamAPI_ISteamClient_GetISteamGenericInterface(self, hSteamUser, hSteamPipe, ptr2); }, py::arg("hSteamUser"), py::arg("hSteamPipe"), py::arg("pchVersion"))
 		.def("GetISteamUserStats", [](ISteamClient* self, HSteamUser hSteamUser, HSteamPipe hSteamPipe, uintptr_t pchVersion) { 
			const char * ptr2 = reinterpret_cast<const char *>(pchVersion);
			return (ISteamUserStats *)SteamAPI_ISteamClient_GetISteamUserStats(self, hSteamUser, hSteamPipe, ptr2); }, py::arg("hSteamUser"), py::arg("hSteamPipe"), py::arg("pchVersion"))
 		.def("GetISteamGameServerStats", [](ISteamClient* self, HSteamUser hSteamuser, HSteamPipe hSteamPipe, uintptr_t pchVersion) { 
			const char * ptr2 = reinterpret_cast<const char *>(pchVersion);
			return (ISteamGameServerStats *)SteamAPI_ISteamClient_GetISteamGameServerStats(self, hSteamuser, hSteamPipe, ptr2); }, py::arg("hSteamuser"), py::arg("hSteamPipe"), py::arg("pchVersion"))
 		.def("GetISteamApps", [](ISteamClient* self, HSteamUser hSteamUser, HSteamPipe hSteamPipe, uintptr_t pchVersion) { 
			const char * ptr2 = reinterpret_cast<const char *>(pchVersion);
			return (ISteamApps *)SteamAPI_ISteamClient_GetISteamApps(self, hSteamUser, hSteamPipe, ptr2); }, py::arg("hSteamUser"), py::arg("hSteamPipe"), py::arg("pchVersion"))
 		.def("GetISteamNetworking", [](ISteamClient* self, HSteamUser hSteamUser, HSteamPipe hSteamPipe, uintptr_t pchVersion) { 
			const char * ptr2 = reinterpret_cast<const char *>(pchVersion);
			return (ISteamNetworking *)SteamAPI_ISteamClient_GetISteamNetworking(self, hSteamUser, hSteamPipe, ptr2); }, py::arg("hSteamUser"), py::arg("hSteamPipe"), py::arg("pchVersion"))
 		.def("GetISteamRemoteStorage", [](ISteamClient* self, HSteamUser hSteamuser, HSteamPipe hSteamPipe, uintptr_t pchVersion) { 
			const char * ptr2 = reinterpret_cast<const char *>(pchVersion);
			return (ISteamRemoteStorage *)SteamAPI_ISteamClient_GetISteamRemoteStorage(self, hSteamuser, hSteamPipe, ptr2); }, py::arg("hSteamuser"), py::arg("hSteamPipe"), py::arg("pchVersion"))
 		.def("GetISteamScreenshots", [](ISteamClient* self, HSteamUser hSteamuser, HSteamPipe hSteamPipe, uintptr_t pchVersion) { 
			const char * ptr2 = reinterpret_cast<const char *>(pchVersion);
			return (ISteamScreenshots *)SteamAPI_ISteamClient_GetISteamScreenshots(self, hSteamuser, hSteamPipe, ptr2); }, py::arg("hSteamuser"), py::arg("hSteamPipe"), py::arg("pchVersion"))
 		.def("GetIPCCallCount", &SteamAPI_ISteamClient_GetIPCCallCount)
 		.def("SetWarningMessageHook", &SteamAPI_ISteamClient_SetWarningMessageHook, py::arg("pFunction"))
 		.def("BShutdownIfAllPipesClosed", &SteamAPI_ISteamClient_BShutdownIfAllPipesClosed)
 		.def("GetISteamHTTP", [](ISteamClient* self, HSteamUser hSteamuser, HSteamPipe hSteamPipe, uintptr_t pchVersion) { 
			const char * ptr2 = reinterpret_cast<const char *>(pchVersion);
			return (ISteamHTTP *)SteamAPI_ISteamClient_GetISteamHTTP(self, hSteamuser, hSteamPipe, ptr2); }, py::arg("hSteamuser"), py::arg("hSteamPipe"), py::arg("pchVersion"))
 		.def("GetISteamController", [](ISteamClient* self, HSteamUser hSteamUser, HSteamPipe hSteamPipe, uintptr_t pchVersion) { 
			const char * ptr2 = reinterpret_cast<const char *>(pchVersion);
			return (ISteamController *)SteamAPI_ISteamClient_GetISteamController(self, hSteamUser, hSteamPipe, ptr2); }, py::arg("hSteamUser"), py::arg("hSteamPipe"), py::arg("pchVersion"))
 		.def("GetISteamUGC", [](ISteamClient* self, HSteamUser hSteamUser, HSteamPipe hSteamPipe, uintptr_t pchVersion) { 
			const char * ptr2 = reinterpret_cast<const char *>(pchVersion);
			return (ISteamUGC *)SteamAPI_ISteamClient_GetISteamUGC(self, hSteamUser, hSteamPipe, ptr2); }, py::arg("hSteamUser"), py::arg("hSteamPipe"), py::arg("pchVersion"))
 		.def("GetISteamMusic", [](ISteamClient* self, HSteamUser hSteamuser, HSteamPipe hSteamPipe, uintptr_t pchVersion) { 
			const char * ptr2 = reinterpret_cast<const char *>(pchVersion);
			return (ISteamMusic *)SteamAPI_ISteamClient_GetISteamMusic(self, hSteamuser, hSteamPipe, ptr2); }, py::arg("hSteamuser"), py::arg("hSteamPipe"), py::arg("pchVersion"))
 		.def("GetISteamHTMLSurface", [](ISteamClient* self, HSteamUser hSteamuser, HSteamPipe hSteamPipe, uintptr_t pchVersion) { 
			const char * ptr2 = reinterpret_cast<const char *>(pchVersion);
			return (ISteamHTMLSurface *)SteamAPI_ISteamClient_GetISteamHTMLSurface(self, hSteamuser, hSteamPipe, ptr2); }, py::arg("hSteamuser"), py::arg("hSteamPipe"), py::arg("pchVersion"))
 		.def("GetISteamInventory", [](ISteamClient* self, HSteamUser hSteamuser, HSteamPipe hSteamPipe, uintptr_t pchVersion) { 
			const char * ptr2 = reinterpret_cast<const char *>(pchVersion);
			return (ISteamInventory *)SteamAPI_ISteamClient_GetISteamInventory(self, hSteamuser, hSteamPipe, ptr2); }, py::arg("hSteamuser"), py::arg("hSteamPipe"), py::arg("pchVersion"))
 		.def("GetISteamVideo", [](ISteamClient* self, HSteamUser hSteamuser, HSteamPipe hSteamPipe, uintptr_t pchVersion) { 
			const char * ptr2 = reinterpret_cast<const char *>(pchVersion);
			return (ISteamVideo *)SteamAPI_ISteamClient_GetISteamVideo(self, hSteamuser, hSteamPipe, ptr2); }, py::arg("hSteamuser"), py::arg("hSteamPipe"), py::arg("pchVersion"))
 		.def("GetISteamParentalSettings", [](ISteamClient* self, HSteamUser hSteamuser, HSteamPipe hSteamPipe, uintptr_t pchVersion) { 
			const char * ptr2 = reinterpret_cast<const char *>(pchVersion);
			return (ISteamParentalSettings *)SteamAPI_ISteamClient_GetISteamParentalSettings(self, hSteamuser, hSteamPipe, ptr2); }, py::arg("hSteamuser"), py::arg("hSteamPipe"), py::arg("pchVersion"))
 		.def("GetISteamInput", [](ISteamClient* self, HSteamUser hSteamUser, HSteamPipe hSteamPipe, uintptr_t pchVersion) { 
			const char * ptr2 = reinterpret_cast<const char *>(pchVersion);
			return (ISteamInput *)SteamAPI_ISteamClient_GetISteamInput(self, hSteamUser, hSteamPipe, ptr2); }, py::arg("hSteamUser"), py::arg("hSteamPipe"), py::arg("pchVersion"))
 		.def("GetISteamParties", [](ISteamClient* self, HSteamUser hSteamUser, HSteamPipe hSteamPipe, uintptr_t pchVersion) { 
			const char * ptr2 = reinterpret_cast<const char *>(pchVersion);
			return (ISteamParties *)SteamAPI_ISteamClient_GetISteamParties(self, hSteamUser, hSteamPipe, ptr2); }, py::arg("hSteamUser"), py::arg("hSteamPipe"), py::arg("pchVersion"))
 		.def("GetISteamRemotePlay", [](ISteamClient* self, HSteamUser hSteamUser, HSteamPipe hSteamPipe, uintptr_t pchVersion) { 
			const char * ptr2 = reinterpret_cast<const char *>(pchVersion);
			return (ISteamRemotePlay *)SteamAPI_ISteamClient_GetISteamRemotePlay(self, hSteamUser, hSteamPipe, ptr2); }, py::arg("hSteamUser"), py::arg("hSteamPipe"), py::arg("pchVersion"))
 	;
 	m.def("SteamUser",[]() -> ISteamUser* { return SteamUser(); }, py::return_value_policy::reference);
 	py::class_<ISteamUser, std::unique_ptr<ISteamUser, py::nodelete>> _isteamuser(m, "ISteamUser");
 	_isteamuser
 		.def_property_readonly("ptr", [](ISteamUser& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def("GetHSteamUser", &SteamAPI_ISteamUser_GetHSteamUser)
 		.def("BLoggedOn", &SteamAPI_ISteamUser_BLoggedOn)
 		.def("GetSteamID", &SteamAPI_ISteamUser_GetSteamID)
 		.def("TrackAppUsageEvent", [](ISteamUser* self, uint64_gameid gameID, int eAppUsageEvent, uintptr_t pchExtraInfo) { 
			const char * ptr2 = reinterpret_cast<const char *>(pchExtraInfo);
			SteamAPI_ISteamUser_TrackAppUsageEvent(self, gameID, eAppUsageEvent, ptr2); }, py::arg("gameID"), py::arg("eAppUsageEvent"), py::arg("pchExtraInfo"))
 		.def("GetUserDataFolder", [](ISteamUser* self, uintptr_t pchBuffer, int cubBuffer) { 
			char * ptr0 = reinterpret_cast<char *>(pchBuffer);
			return (bool)SteamAPI_ISteamUser_GetUserDataFolder(self, ptr0, cubBuffer); }, py::arg("pchBuffer"), py::arg("cubBuffer"))
 		.def("StartVoiceRecording", &SteamAPI_ISteamUser_StartVoiceRecording)
 		.def("StopVoiceRecording", &SteamAPI_ISteamUser_StopVoiceRecording)
 		.def("GetAvailableVoice", [](ISteamUser* self, uintptr_t pcbCompressed, uintptr_t pcbUncompressed_Deprecated, uint32 nUncompressedVoiceDesiredSampleRate_Deprecated) { 
			uint32 * ptr0 = reinterpret_cast<uint32 *>(pcbCompressed);
			uint32 * ptr1 = reinterpret_cast<uint32 *>(pcbUncompressed_Deprecated);
			return (EVoiceResult)SteamAPI_ISteamUser_GetAvailableVoice(self, ptr0, ptr1, nUncompressedVoiceDesiredSampleRate_Deprecated); }, py::arg("pcbCompressed"), py::arg("pcbUncompressed_Deprecated"), py::arg("nUncompressedVoiceDesiredSampleRate_Deprecated"))
 		.def("GetVoice", [](ISteamUser* self, bool bWantCompressed, uintptr_t pDestBuffer, uint32 cbDestBufferSize, uintptr_t nBytesWritten, bool bWantUncompressed_Deprecated, uintptr_t pUncompressedDestBuffer_Deprecated, uint32 cbUncompressedDestBufferSize_Deprecated, uintptr_t nUncompressBytesWritten_Deprecated, uint32 nUncompressedVoiceDesiredSampleRate_Deprecated) { 
			void * ptr1 = reinterpret_cast<void *>(pDestBuffer);
			uint32 * ptr3 = reinterpret_cast<uint32 *>(nBytesWritten);
			void * ptr5 = reinterpret_cast<void *>(pUncompressedDestBuffer_Deprecated);
			uint32 * ptr7 = reinterpret_cast<uint32 *>(nUncompressBytesWritten_Deprecated);
			return (EVoiceResult)SteamAPI_ISteamUser_GetVoice(self, bWantCompressed, ptr1, cbDestBufferSize, ptr3, bWantUncompressed_Deprecated, ptr5, cbUncompressedDestBufferSize_Deprecated, ptr7, nUncompressedVoiceDesiredSampleRate_Deprecated); }, py::arg("bWantCompressed"), py::arg("pDestBuffer"), py::arg("cbDestBufferSize"), py::arg("nBytesWritten"), py::arg("bWantUncompressed_Deprecated"), py::arg("pUncompressedDestBuffer_Deprecated"), py::arg("cbUncompressedDestBufferSize_Deprecated"), py::arg("nUncompressBytesWritten_Deprecated"), py::arg("nUncompressedVoiceDesiredSampleRate_Deprecated"))
 		.def("DecompressVoice", [](ISteamUser* self, uintptr_t pCompressed, uint32 cbCompressed, uintptr_t pDestBuffer, uint32 cbDestBufferSize, uintptr_t nBytesWritten, uint32 nDesiredSampleRate) { 
			const void * ptr0 = reinterpret_cast<const void *>(pCompressed);
			void * ptr2 = reinterpret_cast<void *>(pDestBuffer);
			uint32 * ptr4 = reinterpret_cast<uint32 *>(nBytesWritten);
			return (EVoiceResult)SteamAPI_ISteamUser_DecompressVoice(self, ptr0, cbCompressed, ptr2, cbDestBufferSize, ptr4, nDesiredSampleRate); }, py::arg("pCompressed"), py::arg("cbCompressed"), py::arg("pDestBuffer"), py::arg("cbDestBufferSize"), py::arg("nBytesWritten"), py::arg("nDesiredSampleRate"))
 		.def("GetVoiceOptimalSampleRate", &SteamAPI_ISteamUser_GetVoiceOptimalSampleRate)
 		.def("GetAuthSessionTicket", [](ISteamUser* self, uintptr_t pTicket, int cbMaxTicket, uintptr_t pcbTicket, uintptr_t pSteamNetworkingIdentity) { 
			void * ptr0 = reinterpret_cast<void *>(pTicket);
			uint32 * ptr2 = reinterpret_cast<uint32 *>(pcbTicket);
			const SteamNetworkingIdentity * ptr3 = reinterpret_cast<const SteamNetworkingIdentity *>(pSteamNetworkingIdentity);
			return (HAuthTicket)SteamAPI_ISteamUser_GetAuthSessionTicket(self, ptr0, cbMaxTicket, ptr2, ptr3); }, py::arg("pTicket"), py::arg("cbMaxTicket"), py::arg("pcbTicket"), py::arg("pSteamNetworkingIdentity"))
 		.def("GetAuthTicketForWebApi", [](ISteamUser* self, uintptr_t pchIdentity) { 
			const char * ptr0 = reinterpret_cast<const char *>(pchIdentity);
			return (HAuthTicket)SteamAPI_ISteamUser_GetAuthTicketForWebApi(self, ptr0); }, py::arg("pchIdentity"))
 		.def("BeginAuthSession", [](ISteamUser* self, uintptr_t pAuthTicket, int cbAuthTicket, uint64_steamid steamID) { 
			const void * ptr0 = reinterpret_cast<const void *>(pAuthTicket);
			return (EBeginAuthSessionResult)SteamAPI_ISteamUser_BeginAuthSession(self, ptr0, cbAuthTicket, steamID); }, py::arg("pAuthTicket"), py::arg("cbAuthTicket"), py::arg("steamID"))
 		.def("EndAuthSession", &SteamAPI_ISteamUser_EndAuthSession, py::arg("steamID"))
 		.def("CancelAuthTicket", &SteamAPI_ISteamUser_CancelAuthTicket, py::arg("hAuthTicket"))
 		.def("UserHasLicenseForApp", &SteamAPI_ISteamUser_UserHasLicenseForApp, py::arg("steamID"), py::arg("appID"))
 		.def("BIsBehindNAT", &SteamAPI_ISteamUser_BIsBehindNAT)
 		.def("AdvertiseGame", &SteamAPI_ISteamUser_AdvertiseGame, py::arg("steamIDGameServer"), py::arg("unIPServer"), py::arg("usPortServer"))
 		.def("RequestEncryptedAppTicket", [](ISteamUser* self, uintptr_t pDataToInclude, int cbDataToInclude) { 
			void * ptr0 = reinterpret_cast<void *>(pDataToInclude);
			return (SteamAPICall_t)SteamAPI_ISteamUser_RequestEncryptedAppTicket(self, ptr0, cbDataToInclude); }, py::arg("pDataToInclude"), py::arg("cbDataToInclude"))
 		.def("GetEncryptedAppTicket", [](ISteamUser* self, uintptr_t pTicket, int cbMaxTicket, uintptr_t pcbTicket) { 
			void * ptr0 = reinterpret_cast<void *>(pTicket);
			uint32 * ptr2 = reinterpret_cast<uint32 *>(pcbTicket);
			return (bool)SteamAPI_ISteamUser_GetEncryptedAppTicket(self, ptr0, cbMaxTicket, ptr2); }, py::arg("pTicket"), py::arg("cbMaxTicket"), py::arg("pcbTicket"))
 		.def("GetGameBadgeLevel", &SteamAPI_ISteamUser_GetGameBadgeLevel, py::arg("nSeries"), py::arg("bFoil"))
 		.def("GetPlayerSteamLevel", &SteamAPI_ISteamUser_GetPlayerSteamLevel)
 		.def("RequestStoreAuthURL", [](ISteamUser* self, uintptr_t pchRedirectURL) { 
			const char * ptr0 = reinterpret_cast<const char *>(pchRedirectURL);
			return (SteamAPICall_t)SteamAPI_ISteamUser_RequestStoreAuthURL(self, ptr0); }, py::arg("pchRedirectURL"))
 		.def("BIsPhoneVerified", &SteamAPI_ISteamUser_BIsPhoneVerified)
 		.def("BIsTwoFactorEnabled", &SteamAPI_ISteamUser_BIsTwoFactorEnabled)
 		.def("BIsPhoneIdentifying", &SteamAPI_ISteamUser_BIsPhoneIdentifying)
 		.def("BIsPhoneRequiringVerification", &SteamAPI_ISteamUser_BIsPhoneRequiringVerification)
 		.def("GetMarketEligibility", &SteamAPI_ISteamUser_GetMarketEligibility)
 		.def("GetDurationControl", &SteamAPI_ISteamUser_GetDurationControl)
 		.def("BSetDurationControlOnlineState", &SteamAPI_ISteamUser_BSetDurationControlOnlineState, py::arg("eNewState"))
 	;
 	m.def("SteamFriends",[]() -> ISteamFriends* { return SteamFriends(); }, py::return_value_policy::reference);
 	py::class_<ISteamFriends, std::unique_ptr<ISteamFriends, py::nodelete>> _isteamfriends(m, "ISteamFriends");
 	_isteamfriends
 		.def_property_readonly("ptr", [](ISteamFriends& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def("GetPersonaName", &SteamAPI_ISteamFriends_GetPersonaName)
 		.def("GetPersonaState", &SteamAPI_ISteamFriends_GetPersonaState)
 		.def("GetFriendCount", &SteamAPI_ISteamFriends_GetFriendCount, py::arg("iFriendFlags"))
 		.def("GetFriendByIndex", &SteamAPI_ISteamFriends_GetFriendByIndex, py::arg("iFriend"), py::arg("iFriendFlags"))
 		.def("GetFriendRelationship", &SteamAPI_ISteamFriends_GetFriendRelationship, py::arg("steamIDFriend"))
 		.def("GetFriendPersonaState", &SteamAPI_ISteamFriends_GetFriendPersonaState, py::arg("steamIDFriend"))
 		.def("GetFriendPersonaName", &SteamAPI_ISteamFriends_GetFriendPersonaName, py::arg("steamIDFriend"))
 		.def("GetFriendGamePlayed", [](ISteamFriends* self, uint64_steamid steamIDFriend, uintptr_t pFriendGameInfo) { 
			FriendGameInfo_t * ptr1 = reinterpret_cast<FriendGameInfo_t *>(pFriendGameInfo);
			return (bool)SteamAPI_ISteamFriends_GetFriendGamePlayed(self, steamIDFriend, ptr1); }, py::arg("steamIDFriend"), py::arg("pFriendGameInfo"))
 		.def("GetFriendPersonaNameHistory", &SteamAPI_ISteamFriends_GetFriendPersonaNameHistory, py::arg("steamIDFriend"), py::arg("iPersonaName"))
 		.def("GetFriendSteamLevel", &SteamAPI_ISteamFriends_GetFriendSteamLevel, py::arg("steamIDFriend"))
 		.def("GetPlayerNickname", &SteamAPI_ISteamFriends_GetPlayerNickname, py::arg("steamIDPlayer"))
 		.def("GetFriendsGroupCount", &SteamAPI_ISteamFriends_GetFriendsGroupCount)
 		.def("GetFriendsGroupIDByIndex", &SteamAPI_ISteamFriends_GetFriendsGroupIDByIndex, py::arg("iFG"))
 		.def("GetFriendsGroupName", &SteamAPI_ISteamFriends_GetFriendsGroupName, py::arg("friendsGroupID"))
 		.def("GetFriendsGroupMembersCount", &SteamAPI_ISteamFriends_GetFriendsGroupMembersCount, py::arg("friendsGroupID"))
 		.def("GetFriendsGroupMembersList", [](ISteamFriends* self, FriendsGroupID_t friendsGroupID, uintptr_t pOutSteamIDMembers, int nMembersCount) { 
			CSteamID * ptr1 = reinterpret_cast<CSteamID *>(pOutSteamIDMembers);
			SteamAPI_ISteamFriends_GetFriendsGroupMembersList(self, friendsGroupID, ptr1, nMembersCount); }, py::arg("friendsGroupID"), py::arg("pOutSteamIDMembers"), py::arg("nMembersCount"))
 		.def("HasFriend", &SteamAPI_ISteamFriends_HasFriend, py::arg("steamIDFriend"), py::arg("iFriendFlags"))
 		.def("GetClanCount", &SteamAPI_ISteamFriends_GetClanCount)
 		.def("GetClanByIndex", &SteamAPI_ISteamFriends_GetClanByIndex, py::arg("iClan"))
 		.def("GetClanName", &SteamAPI_ISteamFriends_GetClanName, py::arg("steamIDClan"))
 		.def("GetClanTag", &SteamAPI_ISteamFriends_GetClanTag, py::arg("steamIDClan"))
 		.def("GetClanActivityCounts", [](ISteamFriends* self, uint64_steamid steamIDClan, uintptr_t pnOnline, uintptr_t pnInGame, uintptr_t pnChatting) { 
			int * ptr1 = reinterpret_cast<int *>(pnOnline);
			int * ptr2 = reinterpret_cast<int *>(pnInGame);
			int * ptr3 = reinterpret_cast<int *>(pnChatting);
			return (bool)SteamAPI_ISteamFriends_GetClanActivityCounts(self, steamIDClan, ptr1, ptr2, ptr3); }, py::arg("steamIDClan"), py::arg("pnOnline"), py::arg("pnInGame"), py::arg("pnChatting"))
 		.def("DownloadClanActivityCounts", [](ISteamFriends* self, uintptr_t psteamIDClans, int cClansToRequest) { 
			CSteamID * ptr0 = reinterpret_cast<CSteamID *>(psteamIDClans);
			return (SteamAPICall_t)SteamAPI_ISteamFriends_DownloadClanActivityCounts(self, ptr0, cClansToRequest); }, py::arg("psteamIDClans"), py::arg("cClansToRequest"))
 		.def("GetFriendCountFromSource", &SteamAPI_ISteamFriends_GetFriendCountFromSource, py::arg("steamIDSource"))
 		.def("GetFriendFromSourceByIndex", &SteamAPI_ISteamFriends_GetFriendFromSourceByIndex, py::arg("steamIDSource"), py::arg("iFriend"))
 		.def("IsUserInSource", &SteamAPI_ISteamFriends_IsUserInSource, py::arg("steamIDUser"), py::arg("steamIDSource"))
 		.def("SetInGameVoiceSpeaking", &SteamAPI_ISteamFriends_SetInGameVoiceSpeaking, py::arg("steamIDUser"), py::arg("bSpeaking"))
 		.def("ActivateGameOverlay", [](ISteamFriends* self, uintptr_t pchDialog) { 
			const char * ptr0 = reinterpret_cast<const char *>(pchDialog);
			SteamAPI_ISteamFriends_ActivateGameOverlay(self, ptr0); }, py::arg("pchDialog"))
 		.def("ActivateGameOverlayToUser", [](ISteamFriends* self, uintptr_t pchDialog, uint64_steamid steamID) { 
			const char * ptr0 = reinterpret_cast<const char *>(pchDialog);
			SteamAPI_ISteamFriends_ActivateGameOverlayToUser(self, ptr0, steamID); }, py::arg("pchDialog"), py::arg("steamID"))
 		.def("ActivateGameOverlayToWebPage", [](ISteamFriends* self, uintptr_t pchURL, EActivateGameOverlayToWebPageMode eMode) { 
			const char * ptr0 = reinterpret_cast<const char *>(pchURL);
			SteamAPI_ISteamFriends_ActivateGameOverlayToWebPage(self, ptr0, eMode); }, py::arg("pchURL"), py::arg("eMode"))
 		.def("ActivateGameOverlayToStore", &SteamAPI_ISteamFriends_ActivateGameOverlayToStore, py::arg("nAppID"), py::arg("eFlag"))
 		.def("SetPlayedWith", &SteamAPI_ISteamFriends_SetPlayedWith, py::arg("steamIDUserPlayedWith"))
 		.def("ActivateGameOverlayInviteDialog", &SteamAPI_ISteamFriends_ActivateGameOverlayInviteDialog, py::arg("steamIDLobby"))
 		.def("GetSmallFriendAvatar", &SteamAPI_ISteamFriends_GetSmallFriendAvatar, py::arg("steamIDFriend"))
 		.def("GetMediumFriendAvatar", &SteamAPI_ISteamFriends_GetMediumFriendAvatar, py::arg("steamIDFriend"))
 		.def("GetLargeFriendAvatar", &SteamAPI_ISteamFriends_GetLargeFriendAvatar, py::arg("steamIDFriend"))
 		.def("RequestUserInformation", &SteamAPI_ISteamFriends_RequestUserInformation, py::arg("steamIDUser"), py::arg("bRequireNameOnly"))
 		.def("RequestClanOfficerList", &SteamAPI_ISteamFriends_RequestClanOfficerList, py::arg("steamIDClan"))
 		.def("GetClanOwner", &SteamAPI_ISteamFriends_GetClanOwner, py::arg("steamIDClan"))
 		.def("GetClanOfficerCount", &SteamAPI_ISteamFriends_GetClanOfficerCount, py::arg("steamIDClan"))
 		.def("GetClanOfficerByIndex", &SteamAPI_ISteamFriends_GetClanOfficerByIndex, py::arg("steamIDClan"), py::arg("iOfficer"))
 		.def("SetRichPresence", [](ISteamFriends* self, uintptr_t pchKey, uintptr_t pchValue) { 
			const char * ptr0 = reinterpret_cast<const char *>(pchKey);
			const char * ptr1 = reinterpret_cast<const char *>(pchValue);
			return (bool)SteamAPI_ISteamFriends_SetRichPresence(self, ptr0, ptr1); }, py::arg("pchKey"), py::arg("pchValue"))
 		.def("ClearRichPresence", &SteamAPI_ISteamFriends_ClearRichPresence)
 		.def("GetFriendRichPresence", [](ISteamFriends* self, uint64_steamid steamIDFriend, uintptr_t pchKey) { 
			const char * ptr1 = reinterpret_cast<const char *>(pchKey);
			return (const char *)SteamAPI_ISteamFriends_GetFriendRichPresence(self, steamIDFriend, ptr1); }, py::arg("steamIDFriend"), py::arg("pchKey"))
 		.def("GetFriendRichPresenceKeyCount", &SteamAPI_ISteamFriends_GetFriendRichPresenceKeyCount, py::arg("steamIDFriend"))
 		.def("GetFriendRichPresenceKeyByIndex", &SteamAPI_ISteamFriends_GetFriendRichPresenceKeyByIndex, py::arg("steamIDFriend"), py::arg("iKey"))
 		.def("RequestFriendRichPresence", &SteamAPI_ISteamFriends_RequestFriendRichPresence, py::arg("steamIDFriend"))
 		.def("InviteUserToGame", [](ISteamFriends* self, uint64_steamid steamIDFriend, uintptr_t pchConnectString) { 
			const char * ptr1 = reinterpret_cast<const char *>(pchConnectString);
			return (bool)SteamAPI_ISteamFriends_InviteUserToGame(self, steamIDFriend, ptr1); }, py::arg("steamIDFriend"), py::arg("pchConnectString"))
 		.def("GetCoplayFriendCount", &SteamAPI_ISteamFriends_GetCoplayFriendCount)
 		.def("GetCoplayFriend", &SteamAPI_ISteamFriends_GetCoplayFriend, py::arg("iCoplayFriend"))
 		.def("GetFriendCoplayTime", &SteamAPI_ISteamFriends_GetFriendCoplayTime, py::arg("steamIDFriend"))
 		.def("GetFriendCoplayGame", &SteamAPI_ISteamFriends_GetFriendCoplayGame, py::arg("steamIDFriend"))
 		.def("JoinClanChatRoom", &SteamAPI_ISteamFriends_JoinClanChatRoom, py::arg("steamIDClan"))
 		.def("LeaveClanChatRoom", &SteamAPI_ISteamFriends_LeaveClanChatRoom, py::arg("steamIDClan"))
 		.def("GetClanChatMemberCount", &SteamAPI_ISteamFriends_GetClanChatMemberCount, py::arg("steamIDClan"))
 		.def("GetChatMemberByIndex", &SteamAPI_ISteamFriends_GetChatMemberByIndex, py::arg("steamIDClan"), py::arg("iUser"))
 		.def("SendClanChatMessage", [](ISteamFriends* self, uint64_steamid steamIDClanChat, uintptr_t pchText) { 
			const char * ptr1 = reinterpret_cast<const char *>(pchText);
			return (bool)SteamAPI_ISteamFriends_SendClanChatMessage(self, steamIDClanChat, ptr1); }, py::arg("steamIDClanChat"), py::arg("pchText"))
 		.def("GetClanChatMessage", [](ISteamFriends* self, uint64_steamid steamIDClanChat, int iMessage, uintptr_t prgchText, int cchTextMax, uintptr_t peChatEntryType, uintptr_t psteamidChatter) { 
			void * ptr2 = reinterpret_cast<void *>(prgchText);
			EChatEntryType * ptr4 = reinterpret_cast<EChatEntryType *>(peChatEntryType);
			CSteamID * ptr5 = reinterpret_cast<CSteamID *>(psteamidChatter);
			return (int)SteamAPI_ISteamFriends_GetClanChatMessage(self, steamIDClanChat, iMessage, ptr2, cchTextMax, ptr4, ptr5); }, py::arg("steamIDClanChat"), py::arg("iMessage"), py::arg("prgchText"), py::arg("cchTextMax"), py::arg("peChatEntryType"), py::arg("psteamidChatter"))
 		.def("IsClanChatAdmin", &SteamAPI_ISteamFriends_IsClanChatAdmin, py::arg("steamIDClanChat"), py::arg("steamIDUser"))
 		.def("IsClanChatWindowOpenInSteam", &SteamAPI_ISteamFriends_IsClanChatWindowOpenInSteam, py::arg("steamIDClanChat"))
 		.def("OpenClanChatWindowInSteam", &SteamAPI_ISteamFriends_OpenClanChatWindowInSteam, py::arg("steamIDClanChat"))
 		.def("CloseClanChatWindowInSteam", &SteamAPI_ISteamFriends_CloseClanChatWindowInSteam, py::arg("steamIDClanChat"))
 		.def("SetListenForFriendsMessages", &SteamAPI_ISteamFriends_SetListenForFriendsMessages, py::arg("bInterceptEnabled"))
 		.def("ReplyToFriendMessage", [](ISteamFriends* self, uint64_steamid steamIDFriend, uintptr_t pchMsgToSend) { 
			const char * ptr1 = reinterpret_cast<const char *>(pchMsgToSend);
			return (bool)SteamAPI_ISteamFriends_ReplyToFriendMessage(self, steamIDFriend, ptr1); }, py::arg("steamIDFriend"), py::arg("pchMsgToSend"))
 		.def("GetFriendMessage", [](ISteamFriends* self, uint64_steamid steamIDFriend, int iMessageID, uintptr_t pvData, int cubData, uintptr_t peChatEntryType) { 
			void * ptr2 = reinterpret_cast<void *>(pvData);
			EChatEntryType * ptr4 = reinterpret_cast<EChatEntryType *>(peChatEntryType);
			return (int)SteamAPI_ISteamFriends_GetFriendMessage(self, steamIDFriend, iMessageID, ptr2, cubData, ptr4); }, py::arg("steamIDFriend"), py::arg("iMessageID"), py::arg("pvData"), py::arg("cubData"), py::arg("peChatEntryType"))
 		.def("GetFollowerCount", &SteamAPI_ISteamFriends_GetFollowerCount, py::arg("steamID"))
 		.def("IsFollowing", &SteamAPI_ISteamFriends_IsFollowing, py::arg("steamID"))
 		.def("EnumerateFollowingList", &SteamAPI_ISteamFriends_EnumerateFollowingList, py::arg("unStartIndex"))
 		.def("IsClanPublic", &SteamAPI_ISteamFriends_IsClanPublic, py::arg("steamIDClan"))
 		.def("IsClanOfficialGameGroup", &SteamAPI_ISteamFriends_IsClanOfficialGameGroup, py::arg("steamIDClan"))
 		.def("GetNumChatsWithUnreadPriorityMessages", &SteamAPI_ISteamFriends_GetNumChatsWithUnreadPriorityMessages)
 		.def("ActivateGameOverlayRemotePlayTogetherInviteDialog", &SteamAPI_ISteamFriends_ActivateGameOverlayRemotePlayTogetherInviteDialog, py::arg("steamIDLobby"))
 		.def("RegisterProtocolInOverlayBrowser", [](ISteamFriends* self, uintptr_t pchProtocol) { 
			const char * ptr0 = reinterpret_cast<const char *>(pchProtocol);
			return (bool)SteamAPI_ISteamFriends_RegisterProtocolInOverlayBrowser(self, ptr0); }, py::arg("pchProtocol"))
 		.def("ActivateGameOverlayInviteDialogConnectString", [](ISteamFriends* self, uintptr_t pchConnectString) { 
			const char * ptr0 = reinterpret_cast<const char *>(pchConnectString);
			SteamAPI_ISteamFriends_ActivateGameOverlayInviteDialogConnectString(self, ptr0); }, py::arg("pchConnectString"))
 		.def("RequestEquippedProfileItems", &SteamAPI_ISteamFriends_RequestEquippedProfileItems, py::arg("steamID"))
 		.def("BHasEquippedProfileItem", &SteamAPI_ISteamFriends_BHasEquippedProfileItem, py::arg("steamID"), py::arg("itemType"))
 		.def("GetProfileItemPropertyString", &SteamAPI_ISteamFriends_GetProfileItemPropertyString, py::arg("steamID"), py::arg("itemType"), py::arg("prop"))
 		.def("GetProfileItemPropertyUint", &SteamAPI_ISteamFriends_GetProfileItemPropertyUint, py::arg("steamID"), py::arg("itemType"), py::arg("prop"))
 	;
 	m.def("SteamUtils",[]() -> ISteamUtils* { return SteamUtils(); }, py::return_value_policy::reference);
 	py::class_<ISteamUtils, std::unique_ptr<ISteamUtils, py::nodelete>> _isteamutils(m, "ISteamUtils");
 	_isteamutils
 		.def_property_readonly("ptr", [](ISteamUtils& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def("GetSecondsSinceAppActive", &SteamAPI_ISteamUtils_GetSecondsSinceAppActive)
 		.def("GetSecondsSinceComputerActive", &SteamAPI_ISteamUtils_GetSecondsSinceComputerActive)
 		.def("GetConnectedUniverse", &SteamAPI_ISteamUtils_GetConnectedUniverse)
 		.def("GetServerRealTime", &SteamAPI_ISteamUtils_GetServerRealTime)
 		.def("GetIPCountry", &SteamAPI_ISteamUtils_GetIPCountry)
 		.def("GetImageSize", [](ISteamUtils* self, int iImage, uintptr_t pnWidth, uintptr_t pnHeight) { 
			uint32 * ptr1 = reinterpret_cast<uint32 *>(pnWidth);
			uint32 * ptr2 = reinterpret_cast<uint32 *>(pnHeight);
			return (bool)SteamAPI_ISteamUtils_GetImageSize(self, iImage, ptr1, ptr2); }, py::arg("iImage"), py::arg("pnWidth"), py::arg("pnHeight"))
 		.def("GetImageRGBA", [](ISteamUtils* self, int iImage, uintptr_t pubDest, int nDestBufferSize) { 
			uint8 * ptr1 = reinterpret_cast<uint8 *>(pubDest);
			return (bool)SteamAPI_ISteamUtils_GetImageRGBA(self, iImage, ptr1, nDestBufferSize); }, py::arg("iImage"), py::arg("pubDest"), py::arg("nDestBufferSize"))
 		.def("GetCurrentBatteryPower", &SteamAPI_ISteamUtils_GetCurrentBatteryPower)
 		.def("GetAppID", &SteamAPI_ISteamUtils_GetAppID)
 		.def("SetOverlayNotificationPosition", &SteamAPI_ISteamUtils_SetOverlayNotificationPosition, py::arg("eNotificationPosition"))
 		.def("IsAPICallCompleted", [](ISteamUtils* self, SteamAPICall_t hSteamAPICall, uintptr_t pbFailed) { 
			bool * ptr1 = reinterpret_cast<bool *>(pbFailed);
			return (bool)SteamAPI_ISteamUtils_IsAPICallCompleted(self, hSteamAPICall, ptr1); }, py::arg("hSteamAPICall"), py::arg("pbFailed"))
 		.def("GetAPICallFailureReason", &SteamAPI_ISteamUtils_GetAPICallFailureReason, py::arg("hSteamAPICall"))
 		.def("GetAPICallResult", [](ISteamUtils* self, SteamAPICall_t hSteamAPICall, uintptr_t pCallback, int cubCallback, int iCallbackExpected, uintptr_t pbFailed) { 
			void * ptr1 = reinterpret_cast<void *>(pCallback);
			bool * ptr4 = reinterpret_cast<bool *>(pbFailed);
			return (bool)SteamAPI_ISteamUtils_GetAPICallResult(self, hSteamAPICall, ptr1, cubCallback, iCallbackExpected, ptr4); }, py::arg("hSteamAPICall"), py::arg("pCallback"), py::arg("cubCallback"), py::arg("iCallbackExpected"), py::arg("pbFailed"))
 		.def("GetIPCCallCount", &SteamAPI_ISteamUtils_GetIPCCallCount)
 		.def("SetWarningMessageHook", &SteamAPI_ISteamUtils_SetWarningMessageHook, py::arg("pFunction"))
 		.def("IsOverlayEnabled", &SteamAPI_ISteamUtils_IsOverlayEnabled)
 		.def("BOverlayNeedsPresent", &SteamAPI_ISteamUtils_BOverlayNeedsPresent)
 		.def("CheckFileSignature", [](ISteamUtils* self, uintptr_t szFileName) { 
			const char * ptr0 = reinterpret_cast<const char *>(szFileName);
			return (SteamAPICall_t)SteamAPI_ISteamUtils_CheckFileSignature(self, ptr0); }, py::arg("szFileName"))
 		.def("ShowGamepadTextInput", [](ISteamUtils* self, EGamepadTextInputMode eInputMode, EGamepadTextInputLineMode eLineInputMode, uintptr_t pchDescription, uint32 unCharMax, uintptr_t pchExistingText) { 
			const char * ptr2 = reinterpret_cast<const char *>(pchDescription);
			const char * ptr4 = reinterpret_cast<const char *>(pchExistingText);
			return (bool)SteamAPI_ISteamUtils_ShowGamepadTextInput(self, eInputMode, eLineInputMode, ptr2, unCharMax, ptr4); }, py::arg("eInputMode"), py::arg("eLineInputMode"), py::arg("pchDescription"), py::arg("unCharMax"), py::arg("pchExistingText"))
 		.def("GetEnteredGamepadTextLength", &SteamAPI_ISteamUtils_GetEnteredGamepadTextLength)
 		.def("GetEnteredGamepadTextInput", [](ISteamUtils* self, uintptr_t pchText, uint32 cchText) { 
			char * ptr0 = reinterpret_cast<char *>(pchText);
			return (bool)SteamAPI_ISteamUtils_GetEnteredGamepadTextInput(self, ptr0, cchText); }, py::arg("pchText"), py::arg("cchText"))
 		.def("GetSteamUILanguage", &SteamAPI_ISteamUtils_GetSteamUILanguage)
 		.def("IsSteamRunningInVR", &SteamAPI_ISteamUtils_IsSteamRunningInVR)
 		.def("SetOverlayNotificationInset", &SteamAPI_ISteamUtils_SetOverlayNotificationInset, py::arg("nHorizontalInset"), py::arg("nVerticalInset"))
 		.def("IsSteamInBigPictureMode", &SteamAPI_ISteamUtils_IsSteamInBigPictureMode)
 		.def("StartVRDashboard", &SteamAPI_ISteamUtils_StartVRDashboard)
 		.def("IsVRHeadsetStreamingEnabled", &SteamAPI_ISteamUtils_IsVRHeadsetStreamingEnabled)
 		.def("SetVRHeadsetStreamingEnabled", &SteamAPI_ISteamUtils_SetVRHeadsetStreamingEnabled, py::arg("bEnabled"))
 		.def("IsSteamChinaLauncher", &SteamAPI_ISteamUtils_IsSteamChinaLauncher)
 		.def("InitFilterText", &SteamAPI_ISteamUtils_InitFilterText, py::arg("unFilterOptions"))
 		.def("FilterText", [](ISteamUtils* self, ETextFilteringContext eContext, uint64_steamid sourceSteamID, uintptr_t pchInputMessage, uintptr_t pchOutFilteredText, uint32 nByteSizeOutFilteredText) { 
			const char * ptr2 = reinterpret_cast<const char *>(pchInputMessage);
			char * ptr3 = reinterpret_cast<char *>(pchOutFilteredText);
			return (int)SteamAPI_ISteamUtils_FilterText(self, eContext, sourceSteamID, ptr2, ptr3, nByteSizeOutFilteredText); }, py::arg("eContext"), py::arg("sourceSteamID"), py::arg("pchInputMessage"), py::arg("pchOutFilteredText"), py::arg("nByteSizeOutFilteredText"))
 		.def("GetIPv6ConnectivityState", &SteamAPI_ISteamUtils_GetIPv6ConnectivityState, py::arg("eProtocol"))
 		.def("IsSteamRunningOnSteamDeck", &SteamAPI_ISteamUtils_IsSteamRunningOnSteamDeck)
 		.def("ShowFloatingGamepadTextInput", &SteamAPI_ISteamUtils_ShowFloatingGamepadTextInput, py::arg("eKeyboardMode"), py::arg("nTextFieldXPosition"), py::arg("nTextFieldYPosition"), py::arg("nTextFieldWidth"), py::arg("nTextFieldHeight"))
 		.def("SetGameLauncherMode", &SteamAPI_ISteamUtils_SetGameLauncherMode, py::arg("bLauncherMode"))
 		.def("DismissFloatingGamepadTextInput", &SteamAPI_ISteamUtils_DismissFloatingGamepadTextInput)
 		.def("DismissGamepadTextInput", &SteamAPI_ISteamUtils_DismissGamepadTextInput)
 	;
 	m.def("SteamMatchmaking",[]() -> ISteamMatchmaking* { return SteamMatchmaking(); }, py::return_value_policy::reference);
 	py::class_<ISteamMatchmaking, std::unique_ptr<ISteamMatchmaking, py::nodelete>> _isteammatchmaking(m, "ISteamMatchmaking");
 	_isteammatchmaking
 		.def_property_readonly("ptr", [](ISteamMatchmaking& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def("GetFavoriteGameCount", &SteamAPI_ISteamMatchmaking_GetFavoriteGameCount)
 		.def("GetFavoriteGame", [](ISteamMatchmaking* self, int iGame, uintptr_t pnAppID, uintptr_t pnIP, uintptr_t pnConnPort, uintptr_t pnQueryPort, uintptr_t punFlags, uintptr_t pRTime32LastPlayedOnServer) { 
			AppId_t * ptr1 = reinterpret_cast<AppId_t *>(pnAppID);
			uint32 * ptr2 = reinterpret_cast<uint32 *>(pnIP);
			uint16 * ptr3 = reinterpret_cast<uint16 *>(pnConnPort);
			uint16 * ptr4 = reinterpret_cast<uint16 *>(pnQueryPort);
			uint32 * ptr5 = reinterpret_cast<uint32 *>(punFlags);
			uint32 * ptr6 = reinterpret_cast<uint32 *>(pRTime32LastPlayedOnServer);
			return (bool)SteamAPI_ISteamMatchmaking_GetFavoriteGame(self, iGame, ptr1, ptr2, ptr3, ptr4, ptr5, ptr6); }, py::arg("iGame"), py::arg("pnAppID"), py::arg("pnIP"), py::arg("pnConnPort"), py::arg("pnQueryPort"), py::arg("punFlags"), py::arg("pRTime32LastPlayedOnServer"))
 		.def("AddFavoriteGame", &SteamAPI_ISteamMatchmaking_AddFavoriteGame, py::arg("nAppID"), py::arg("nIP"), py::arg("nConnPort"), py::arg("nQueryPort"), py::arg("unFlags"), py::arg("rTime32LastPlayedOnServer"))
 		.def("RemoveFavoriteGame", &SteamAPI_ISteamMatchmaking_RemoveFavoriteGame, py::arg("nAppID"), py::arg("nIP"), py::arg("nConnPort"), py::arg("nQueryPort"), py::arg("unFlags"))
 		.def("RequestLobbyList", &SteamAPI_ISteamMatchmaking_RequestLobbyList)
 		.def("AddRequestLobbyListStringFilter", [](ISteamMatchmaking* self, uintptr_t pchKeyToMatch, uintptr_t pchValueToMatch, ELobbyComparison eComparisonType) { 
			const char * ptr0 = reinterpret_cast<const char *>(pchKeyToMatch);
			const char * ptr1 = reinterpret_cast<const char *>(pchValueToMatch);
			SteamAPI_ISteamMatchmaking_AddRequestLobbyListStringFilter(self, ptr0, ptr1, eComparisonType); }, py::arg("pchKeyToMatch"), py::arg("pchValueToMatch"), py::arg("eComparisonType"))
 		.def("AddRequestLobbyListNumericalFilter", [](ISteamMatchmaking* self, uintptr_t pchKeyToMatch, int nValueToMatch, ELobbyComparison eComparisonType) { 
			const char * ptr0 = reinterpret_cast<const char *>(pchKeyToMatch);
			SteamAPI_ISteamMatchmaking_AddRequestLobbyListNumericalFilter(self, ptr0, nValueToMatch, eComparisonType); }, py::arg("pchKeyToMatch"), py::arg("nValueToMatch"), py::arg("eComparisonType"))
 		.def("AddRequestLobbyListNearValueFilter", [](ISteamMatchmaking* self, uintptr_t pchKeyToMatch, int nValueToBeCloseTo) { 
			const char * ptr0 = reinterpret_cast<const char *>(pchKeyToMatch);
			SteamAPI_ISteamMatchmaking_AddRequestLobbyListNearValueFilter(self, ptr0, nValueToBeCloseTo); }, py::arg("pchKeyToMatch"), py::arg("nValueToBeCloseTo"))
 		.def("AddRequestLobbyListFilterSlotsAvailable", &SteamAPI_ISteamMatchmaking_AddRequestLobbyListFilterSlotsAvailable, py::arg("nSlotsAvailable"))
 		.def("AddRequestLobbyListDistanceFilter", &SteamAPI_ISteamMatchmaking_AddRequestLobbyListDistanceFilter, py::arg("eLobbyDistanceFilter"))
 		.def("AddRequestLobbyListResultCountFilter", &SteamAPI_ISteamMatchmaking_AddRequestLobbyListResultCountFilter, py::arg("cMaxResults"))
 		.def("AddRequestLobbyListCompatibleMembersFilter", &SteamAPI_ISteamMatchmaking_AddRequestLobbyListCompatibleMembersFilter, py::arg("steamIDLobby"))
 		.def("GetLobbyByIndex", &SteamAPI_ISteamMatchmaking_GetLobbyByIndex, py::arg("iLobby"))
 		.def("CreateLobby", &SteamAPI_ISteamMatchmaking_CreateLobby, py::arg("eLobbyType"), py::arg("cMaxMembers"))
 		.def("JoinLobby", &SteamAPI_ISteamMatchmaking_JoinLobby, py::arg("steamIDLobby"))
 		.def("LeaveLobby", &SteamAPI_ISteamMatchmaking_LeaveLobby, py::arg("steamIDLobby"))
 		.def("InviteUserToLobby", &SteamAPI_ISteamMatchmaking_InviteUserToLobby, py::arg("steamIDLobby"), py::arg("steamIDInvitee"))
 		.def("GetNumLobbyMembers", &SteamAPI_ISteamMatchmaking_GetNumLobbyMembers, py::arg("steamIDLobby"))
 		.def("GetLobbyMemberByIndex", &SteamAPI_ISteamMatchmaking_GetLobbyMemberByIndex, py::arg("steamIDLobby"), py::arg("iMember"))
 		.def("GetLobbyData", [](ISteamMatchmaking* self, uint64_steamid steamIDLobby, uintptr_t pchKey) { 
			const char * ptr1 = reinterpret_cast<const char *>(pchKey);
			return (const char *)SteamAPI_ISteamMatchmaking_GetLobbyData(self, steamIDLobby, ptr1); }, py::arg("steamIDLobby"), py::arg("pchKey"))
 		.def("SetLobbyData", [](ISteamMatchmaking* self, uint64_steamid steamIDLobby, uintptr_t pchKey, uintptr_t pchValue) { 
			const char * ptr1 = reinterpret_cast<const char *>(pchKey);
			const char * ptr2 = reinterpret_cast<const char *>(pchValue);
			return (bool)SteamAPI_ISteamMatchmaking_SetLobbyData(self, steamIDLobby, ptr1, ptr2); }, py::arg("steamIDLobby"), py::arg("pchKey"), py::arg("pchValue"))
 		.def("GetLobbyDataCount", &SteamAPI_ISteamMatchmaking_GetLobbyDataCount, py::arg("steamIDLobby"))
 		.def("GetLobbyDataByIndex", [](ISteamMatchmaking* self, uint64_steamid steamIDLobby, int iLobbyData, uintptr_t pchKey, int cchKeyBufferSize, uintptr_t pchValue, int cchValueBufferSize) { 
			char * ptr2 = reinterpret_cast<char *>(pchKey);
			char * ptr4 = reinterpret_cast<char *>(pchValue);
			return (bool)SteamAPI_ISteamMatchmaking_GetLobbyDataByIndex(self, steamIDLobby, iLobbyData, ptr2, cchKeyBufferSize, ptr4, cchValueBufferSize); }, py::arg("steamIDLobby"), py::arg("iLobbyData"), py::arg("pchKey"), py::arg("cchKeyBufferSize"), py::arg("pchValue"), py::arg("cchValueBufferSize"))
 		.def("DeleteLobbyData", [](ISteamMatchmaking* self, uint64_steamid steamIDLobby, uintptr_t pchKey) { 
			const char * ptr1 = reinterpret_cast<const char *>(pchKey);
			return (bool)SteamAPI_ISteamMatchmaking_DeleteLobbyData(self, steamIDLobby, ptr1); }, py::arg("steamIDLobby"), py::arg("pchKey"))
 		.def("GetLobbyMemberData", [](ISteamMatchmaking* self, uint64_steamid steamIDLobby, uint64_steamid steamIDUser, uintptr_t pchKey) { 
			const char * ptr2 = reinterpret_cast<const char *>(pchKey);
			return (const char *)SteamAPI_ISteamMatchmaking_GetLobbyMemberData(self, steamIDLobby, steamIDUser, ptr2); }, py::arg("steamIDLobby"), py::arg("steamIDUser"), py::arg("pchKey"))
 		.def("SetLobbyMemberData", [](ISteamMatchmaking* self, uint64_steamid steamIDLobby, uintptr_t pchKey, uintptr_t pchValue) { 
			const char * ptr1 = reinterpret_cast<const char *>(pchKey);
			const char * ptr2 = reinterpret_cast<const char *>(pchValue);
			SteamAPI_ISteamMatchmaking_SetLobbyMemberData(self, steamIDLobby, ptr1, ptr2); }, py::arg("steamIDLobby"), py::arg("pchKey"), py::arg("pchValue"))
 		.def("SendLobbyChatMsg", [](ISteamMatchmaking* self, uint64_steamid steamIDLobby, uintptr_t pvMsgBody, int cubMsgBody) { 
			const void * ptr1 = reinterpret_cast<const void *>(pvMsgBody);
			return (bool)SteamAPI_ISteamMatchmaking_SendLobbyChatMsg(self, steamIDLobby, ptr1, cubMsgBody); }, py::arg("steamIDLobby"), py::arg("pvMsgBody"), py::arg("cubMsgBody"))
 		.def("GetLobbyChatEntry", [](ISteamMatchmaking* self, uint64_steamid steamIDLobby, int iChatID, uintptr_t pSteamIDUser, uintptr_t pvData, int cubData, uintptr_t peChatEntryType) { 
			CSteamID * ptr2 = reinterpret_cast<CSteamID *>(pSteamIDUser);
			void * ptr3 = reinterpret_cast<void *>(pvData);
			EChatEntryType * ptr5 = reinterpret_cast<EChatEntryType *>(peChatEntryType);
			return (int)SteamAPI_ISteamMatchmaking_GetLobbyChatEntry(self, steamIDLobby, iChatID, ptr2, ptr3, cubData, ptr5); }, py::arg("steamIDLobby"), py::arg("iChatID"), py::arg("pSteamIDUser"), py::arg("pvData"), py::arg("cubData"), py::arg("peChatEntryType"))
 		.def("RequestLobbyData", &SteamAPI_ISteamMatchmaking_RequestLobbyData, py::arg("steamIDLobby"))
 		.def("SetLobbyGameServer", &SteamAPI_ISteamMatchmaking_SetLobbyGameServer, py::arg("steamIDLobby"), py::arg("unGameServerIP"), py::arg("unGameServerPort"), py::arg("steamIDGameServer"))
 		.def("GetLobbyGameServer", [](ISteamMatchmaking* self, uint64_steamid steamIDLobby, uintptr_t punGameServerIP, uintptr_t punGameServerPort, uintptr_t psteamIDGameServer) { 
			uint32 * ptr1 = reinterpret_cast<uint32 *>(punGameServerIP);
			uint16 * ptr2 = reinterpret_cast<uint16 *>(punGameServerPort);
			CSteamID * ptr3 = reinterpret_cast<CSteamID *>(psteamIDGameServer);
			return (bool)SteamAPI_ISteamMatchmaking_GetLobbyGameServer(self, steamIDLobby, ptr1, ptr2, ptr3); }, py::arg("steamIDLobby"), py::arg("punGameServerIP"), py::arg("punGameServerPort"), py::arg("psteamIDGameServer"))
 		.def("SetLobbyMemberLimit", &SteamAPI_ISteamMatchmaking_SetLobbyMemberLimit, py::arg("steamIDLobby"), py::arg("cMaxMembers"))
 		.def("GetLobbyMemberLimit", &SteamAPI_ISteamMatchmaking_GetLobbyMemberLimit, py::arg("steamIDLobby"))
 		.def("SetLobbyType", &SteamAPI_ISteamMatchmaking_SetLobbyType, py::arg("steamIDLobby"), py::arg("eLobbyType"))
 		.def("SetLobbyJoinable", &SteamAPI_ISteamMatchmaking_SetLobbyJoinable, py::arg("steamIDLobby"), py::arg("bLobbyJoinable"))
 		.def("GetLobbyOwner", &SteamAPI_ISteamMatchmaking_GetLobbyOwner, py::arg("steamIDLobby"))
 		.def("SetLobbyOwner", &SteamAPI_ISteamMatchmaking_SetLobbyOwner, py::arg("steamIDLobby"), py::arg("steamIDNewOwner"))
 		.def("SetLinkedLobby", &SteamAPI_ISteamMatchmaking_SetLinkedLobby, py::arg("steamIDLobby"), py::arg("steamIDLobbyDependent"))
 	;
 	py::class_<ISteamMatchmakingServerListResponse, std::unique_ptr<ISteamMatchmakingServerListResponse, py::nodelete>> _isteammatchmakingserverlistresponse(m, "ISteamMatchmakingServerListResponse");
 	_isteammatchmakingserverlistresponse
 		.def_property_readonly("ptr", [](ISteamMatchmakingServerListResponse& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def("ServerResponded", &SteamAPI_ISteamMatchmakingServerListResponse_ServerResponded, py::arg("hRequest"), py::arg("iServer"))
 		.def("ServerFailedToRespond", &SteamAPI_ISteamMatchmakingServerListResponse_ServerFailedToRespond, py::arg("hRequest"), py::arg("iServer"))
 		.def("RefreshComplete", &SteamAPI_ISteamMatchmakingServerListResponse_RefreshComplete, py::arg("hRequest"), py::arg("response"))
 	;
 	py::class_<ISteamMatchmakingPingResponse, std::unique_ptr<ISteamMatchmakingPingResponse, py::nodelete>> _isteammatchmakingpingresponse(m, "ISteamMatchmakingPingResponse");
 	_isteammatchmakingpingresponse
 		.def_property_readonly("ptr", [](ISteamMatchmakingPingResponse& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def("ServerResponded", [](ISteamMatchmakingPingResponse* self, uintptr_t server) { 
			gameserveritem_t & ptr0 = *reinterpret_cast<gameserveritem_t *>(server);
			SteamAPI_ISteamMatchmakingPingResponse_ServerResponded(self, ptr0); }, py::arg("server"))
 		.def("ServerFailedToRespond", &SteamAPI_ISteamMatchmakingPingResponse_ServerFailedToRespond)
 	;
 	py::class_<ISteamMatchmakingPlayersResponse, std::unique_ptr<ISteamMatchmakingPlayersResponse, py::nodelete>> _isteammatchmakingplayersresponse(m, "ISteamMatchmakingPlayersResponse");
 	_isteammatchmakingplayersresponse
 		.def_property_readonly("ptr", [](ISteamMatchmakingPlayersResponse& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def("AddPlayerToList", [](ISteamMatchmakingPlayersResponse* self, uintptr_t pchName, int nScore, float flTimePlayed) { 
			const char * ptr0 = reinterpret_cast<const char *>(pchName);
			SteamAPI_ISteamMatchmakingPlayersResponse_AddPlayerToList(self, ptr0, nScore, flTimePlayed); }, py::arg("pchName"), py::arg("nScore"), py::arg("flTimePlayed"))
 		.def("PlayersFailedToRespond", &SteamAPI_ISteamMatchmakingPlayersResponse_PlayersFailedToRespond)
 		.def("PlayersRefreshComplete", &SteamAPI_ISteamMatchmakingPlayersResponse_PlayersRefreshComplete)
 	;
 	py::class_<ISteamMatchmakingRulesResponse, std::unique_ptr<ISteamMatchmakingRulesResponse, py::nodelete>> _isteammatchmakingrulesresponse(m, "ISteamMatchmakingRulesResponse");
 	_isteammatchmakingrulesresponse
 		.def_property_readonly("ptr", [](ISteamMatchmakingRulesResponse& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def("RulesResponded", [](ISteamMatchmakingRulesResponse* self, uintptr_t pchRule, uintptr_t pchValue) { 
			const char * ptr0 = reinterpret_cast<const char *>(pchRule);
			const char * ptr1 = reinterpret_cast<const char *>(pchValue);
			SteamAPI_ISteamMatchmakingRulesResponse_RulesResponded(self, ptr0, ptr1); }, py::arg("pchRule"), py::arg("pchValue"))
 		.def("RulesFailedToRespond", &SteamAPI_ISteamMatchmakingRulesResponse_RulesFailedToRespond)
 		.def("RulesRefreshComplete", &SteamAPI_ISteamMatchmakingRulesResponse_RulesRefreshComplete)
 	;
 	m.def("SteamMatchmakingServers",[]() -> ISteamMatchmakingServers* { return SteamMatchmakingServers(); }, py::return_value_policy::reference);
 	py::class_<ISteamMatchmakingServers, std::unique_ptr<ISteamMatchmakingServers, py::nodelete>> _isteammatchmakingservers(m, "ISteamMatchmakingServers");
 	_isteammatchmakingservers
 		.def_property_readonly("ptr", [](ISteamMatchmakingServers& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def("RequestInternetServerList", [](ISteamMatchmakingServers* self, AppId_t iApp, uintptr_t ppchFilters, uint32 nFilters, uintptr_t pRequestServersResponse) { 
			MatchMakingKeyValuePair_t ** ptr1 = reinterpret_cast<MatchMakingKeyValuePair_t **>(ppchFilters);
			ISteamMatchmakingServerListResponse * ptr3 = reinterpret_cast<ISteamMatchmakingServerListResponse *>(pRequestServersResponse);
			return (HServerListRequest)SteamAPI_ISteamMatchmakingServers_RequestInternetServerList(self, iApp, ptr1, nFilters, ptr3); }, py::arg("iApp"), py::arg("ppchFilters"), py::arg("nFilters"), py::arg("pRequestServersResponse"))
 		.def("RequestLANServerList", [](ISteamMatchmakingServers* self, AppId_t iApp, uintptr_t pRequestServersResponse) { 
			ISteamMatchmakingServerListResponse * ptr1 = reinterpret_cast<ISteamMatchmakingServerListResponse *>(pRequestServersResponse);
			return (HServerListRequest)SteamAPI_ISteamMatchmakingServers_RequestLANServerList(self, iApp, ptr1); }, py::arg("iApp"), py::arg("pRequestServersResponse"))
 		.def("RequestFriendsServerList", [](ISteamMatchmakingServers* self, AppId_t iApp, uintptr_t ppchFilters, uint32 nFilters, uintptr_t pRequestServersResponse) { 
			MatchMakingKeyValuePair_t ** ptr1 = reinterpret_cast<MatchMakingKeyValuePair_t **>(ppchFilters);
			ISteamMatchmakingServerListResponse * ptr3 = reinterpret_cast<ISteamMatchmakingServerListResponse *>(pRequestServersResponse);
			return (HServerListRequest)SteamAPI_ISteamMatchmakingServers_RequestFriendsServerList(self, iApp, ptr1, nFilters, ptr3); }, py::arg("iApp"), py::arg("ppchFilters"), py::arg("nFilters"), py::arg("pRequestServersResponse"))
 		.def("RequestFavoritesServerList", [](ISteamMatchmakingServers* self, AppId_t iApp, uintptr_t ppchFilters, uint32 nFilters, uintptr_t pRequestServersResponse) { 
			MatchMakingKeyValuePair_t ** ptr1 = reinterpret_cast<MatchMakingKeyValuePair_t **>(ppchFilters);
			ISteamMatchmakingServerListResponse * ptr3 = reinterpret_cast<ISteamMatchmakingServerListResponse *>(pRequestServersResponse);
			return (HServerListRequest)SteamAPI_ISteamMatchmakingServers_RequestFavoritesServerList(self, iApp, ptr1, nFilters, ptr3); }, py::arg("iApp"), py::arg("ppchFilters"), py::arg("nFilters"), py::arg("pRequestServersResponse"))
 		.def("RequestHistoryServerList", [](ISteamMatchmakingServers* self, AppId_t iApp, uintptr_t ppchFilters, uint32 nFilters, uintptr_t pRequestServersResponse) { 
			MatchMakingKeyValuePair_t ** ptr1 = reinterpret_cast<MatchMakingKeyValuePair_t **>(ppchFilters);
			ISteamMatchmakingServerListResponse * ptr3 = reinterpret_cast<ISteamMatchmakingServerListResponse *>(pRequestServersResponse);
			return (HServerListRequest)SteamAPI_ISteamMatchmakingServers_RequestHistoryServerList(self, iApp, ptr1, nFilters, ptr3); }, py::arg("iApp"), py::arg("ppchFilters"), py::arg("nFilters"), py::arg("pRequestServersResponse"))
 		.def("RequestSpectatorServerList", [](ISteamMatchmakingServers* self, AppId_t iApp, uintptr_t ppchFilters, uint32 nFilters, uintptr_t pRequestServersResponse) { 
			MatchMakingKeyValuePair_t ** ptr1 = reinterpret_cast<MatchMakingKeyValuePair_t **>(ppchFilters);
			ISteamMatchmakingServerListResponse * ptr3 = reinterpret_cast<ISteamMatchmakingServerListResponse *>(pRequestServersResponse);
			return (HServerListRequest)SteamAPI_ISteamMatchmakingServers_RequestSpectatorServerList(self, iApp, ptr1, nFilters, ptr3); }, py::arg("iApp"), py::arg("ppchFilters"), py::arg("nFilters"), py::arg("pRequestServersResponse"))
 		.def("ReleaseRequest", &SteamAPI_ISteamMatchmakingServers_ReleaseRequest, py::arg("hServerListRequest"))
 		.def("GetServerDetails", &SteamAPI_ISteamMatchmakingServers_GetServerDetails, py::arg("hRequest"), py::arg("iServer"))
 		.def("CancelQuery", &SteamAPI_ISteamMatchmakingServers_CancelQuery, py::arg("hRequest"))
 		.def("RefreshQuery", &SteamAPI_ISteamMatchmakingServers_RefreshQuery, py::arg("hRequest"))
 		.def("IsRefreshing", &SteamAPI_ISteamMatchmakingServers_IsRefreshing, py::arg("hRequest"))
 		.def("GetServerCount", &SteamAPI_ISteamMatchmakingServers_GetServerCount, py::arg("hRequest"))
 		.def("RefreshServer", &SteamAPI_ISteamMatchmakingServers_RefreshServer, py::arg("hRequest"), py::arg("iServer"))
 		.def("PingServer", [](ISteamMatchmakingServers* self, uint32 unIP, uint16 usPort, uintptr_t pRequestServersResponse) { 
			ISteamMatchmakingPingResponse * ptr2 = reinterpret_cast<ISteamMatchmakingPingResponse *>(pRequestServersResponse);
			return (HServerQuery)SteamAPI_ISteamMatchmakingServers_PingServer(self, unIP, usPort, ptr2); }, py::arg("unIP"), py::arg("usPort"), py::arg("pRequestServersResponse"))
 		.def("PlayerDetails", [](ISteamMatchmakingServers* self, uint32 unIP, uint16 usPort, uintptr_t pRequestServersResponse) { 
			ISteamMatchmakingPlayersResponse * ptr2 = reinterpret_cast<ISteamMatchmakingPlayersResponse *>(pRequestServersResponse);
			return (HServerQuery)SteamAPI_ISteamMatchmakingServers_PlayerDetails(self, unIP, usPort, ptr2); }, py::arg("unIP"), py::arg("usPort"), py::arg("pRequestServersResponse"))
 		.def("ServerRules", [](ISteamMatchmakingServers* self, uint32 unIP, uint16 usPort, uintptr_t pRequestServersResponse) { 
			ISteamMatchmakingRulesResponse * ptr2 = reinterpret_cast<ISteamMatchmakingRulesResponse *>(pRequestServersResponse);
			return (HServerQuery)SteamAPI_ISteamMatchmakingServers_ServerRules(self, unIP, usPort, ptr2); }, py::arg("unIP"), py::arg("usPort"), py::arg("pRequestServersResponse"))
 		.def("CancelServerQuery", &SteamAPI_ISteamMatchmakingServers_CancelServerQuery, py::arg("hServerQuery"))
 	;
 	m.def("SteamParties",[]() -> ISteamParties* { return SteamParties(); }, py::return_value_policy::reference);
 	py::class_<ISteamParties, std::unique_ptr<ISteamParties, py::nodelete>> _isteamparties(m, "ISteamParties");
 	_isteamparties
 		.def_property_readonly("ptr", [](ISteamParties& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def("GetNumActiveBeacons", &SteamAPI_ISteamParties_GetNumActiveBeacons)
 		.def("GetBeaconByIndex", &SteamAPI_ISteamParties_GetBeaconByIndex, py::arg("unIndex"))
 		.def("GetBeaconDetails", [](ISteamParties* self, PartyBeaconID_t ulBeaconID, uintptr_t pSteamIDBeaconOwner, uintptr_t pLocation, uintptr_t pchMetadata, int cchMetadata) { 
			CSteamID * ptr1 = reinterpret_cast<CSteamID *>(pSteamIDBeaconOwner);
			SteamPartyBeaconLocation_t * ptr2 = reinterpret_cast<SteamPartyBeaconLocation_t *>(pLocation);
			char * ptr3 = reinterpret_cast<char *>(pchMetadata);
			return (bool)SteamAPI_ISteamParties_GetBeaconDetails(self, ulBeaconID, ptr1, ptr2, ptr3, cchMetadata); }, py::arg("ulBeaconID"), py::arg("pSteamIDBeaconOwner"), py::arg("pLocation"), py::arg("pchMetadata"), py::arg("cchMetadata"))
 		.def("JoinParty", &SteamAPI_ISteamParties_JoinParty, py::arg("ulBeaconID"))
 		.def("GetNumAvailableBeaconLocations", [](ISteamParties* self, uintptr_t puNumLocations) { 
			uint32 * ptr0 = reinterpret_cast<uint32 *>(puNumLocations);
			return (bool)SteamAPI_ISteamParties_GetNumAvailableBeaconLocations(self, ptr0); }, py::arg("puNumLocations"))
 		.def("GetAvailableBeaconLocations", [](ISteamParties* self, uintptr_t pLocationList, uint32 uMaxNumLocations) { 
			SteamPartyBeaconLocation_t * ptr0 = reinterpret_cast<SteamPartyBeaconLocation_t *>(pLocationList);
			return (bool)SteamAPI_ISteamParties_GetAvailableBeaconLocations(self, ptr0, uMaxNumLocations); }, py::arg("pLocationList"), py::arg("uMaxNumLocations"))
 		.def("CreateBeacon", [](ISteamParties* self, uint32 unOpenSlots, uintptr_t pBeaconLocation, uintptr_t pchConnectString, uintptr_t pchMetadata) { 
			SteamPartyBeaconLocation_t * ptr1 = reinterpret_cast<SteamPartyBeaconLocation_t *>(pBeaconLocation);
			const char * ptr2 = reinterpret_cast<const char *>(pchConnectString);
			const char * ptr3 = reinterpret_cast<const char *>(pchMetadata);
			return (SteamAPICall_t)SteamAPI_ISteamParties_CreateBeacon(self, unOpenSlots, ptr1, ptr2, ptr3); }, py::arg("unOpenSlots"), py::arg("pBeaconLocation"), py::arg("pchConnectString"), py::arg("pchMetadata"))
 		.def("OnReservationCompleted", &SteamAPI_ISteamParties_OnReservationCompleted, py::arg("ulBeacon"), py::arg("steamIDUser"))
 		.def("CancelReservation", &SteamAPI_ISteamParties_CancelReservation, py::arg("ulBeacon"), py::arg("steamIDUser"))
 		.def("ChangeNumOpenSlots", &SteamAPI_ISteamParties_ChangeNumOpenSlots, py::arg("ulBeacon"), py::arg("unOpenSlots"))
 		.def("DestroyBeacon", &SteamAPI_ISteamParties_DestroyBeacon, py::arg("ulBeacon"))
 		.def("GetBeaconLocationData", [](ISteamParties* self, SteamPartyBeaconLocation_t BeaconLocation, ESteamPartyBeaconLocationData eData, uintptr_t pchDataStringOut, int cchDataStringOut) { 
			char * ptr2 = reinterpret_cast<char *>(pchDataStringOut);
			return (bool)SteamAPI_ISteamParties_GetBeaconLocationData(self, BeaconLocation, eData, ptr2, cchDataStringOut); }, py::arg("BeaconLocation"), py::arg("eData"), py::arg("pchDataStringOut"), py::arg("cchDataStringOut"))
 	;
 	m.def("SteamRemoteStorage",[]() -> ISteamRemoteStorage* { return SteamRemoteStorage(); }, py::return_value_policy::reference);
 	py::class_<ISteamRemoteStorage, std::unique_ptr<ISteamRemoteStorage, py::nodelete>> _isteamremotestorage(m, "ISteamRemoteStorage");
 	_isteamremotestorage
 		.def_property_readonly("ptr", [](ISteamRemoteStorage& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def("FileWrite", [](ISteamRemoteStorage* self, uintptr_t pchFile, uintptr_t pvData, int32 cubData) { 
			const char * ptr0 = reinterpret_cast<const char *>(pchFile);
			const void * ptr1 = reinterpret_cast<const void *>(pvData);
			return (bool)SteamAPI_ISteamRemoteStorage_FileWrite(self, ptr0, ptr1, cubData); }, py::arg("pchFile"), py::arg("pvData"), py::arg("cubData"))
 		.def("FileRead", [](ISteamRemoteStorage* self, uintptr_t pchFile, uintptr_t pvData, int32 cubDataToRead) { 
			const char * ptr0 = reinterpret_cast<const char *>(pchFile);
			void * ptr1 = reinterpret_cast<void *>(pvData);
			return (int32)SteamAPI_ISteamRemoteStorage_FileRead(self, ptr0, ptr1, cubDataToRead); }, py::arg("pchFile"), py::arg("pvData"), py::arg("cubDataToRead"))
 		.def("FileWriteAsync", [](ISteamRemoteStorage* self, uintptr_t pchFile, uintptr_t pvData, uint32 cubData) { 
			const char * ptr0 = reinterpret_cast<const char *>(pchFile);
			const void * ptr1 = reinterpret_cast<const void *>(pvData);
			return (SteamAPICall_t)SteamAPI_ISteamRemoteStorage_FileWriteAsync(self, ptr0, ptr1, cubData); }, py::arg("pchFile"), py::arg("pvData"), py::arg("cubData"))
 		.def("FileReadAsync", [](ISteamRemoteStorage* self, uintptr_t pchFile, uint32 nOffset, uint32 cubToRead) { 
			const char * ptr0 = reinterpret_cast<const char *>(pchFile);
			return (SteamAPICall_t)SteamAPI_ISteamRemoteStorage_FileReadAsync(self, ptr0, nOffset, cubToRead); }, py::arg("pchFile"), py::arg("nOffset"), py::arg("cubToRead"))
 		.def("FileReadAsyncComplete", [](ISteamRemoteStorage* self, SteamAPICall_t hReadCall, uintptr_t pvBuffer, uint32 cubToRead) { 
			void * ptr1 = reinterpret_cast<void *>(pvBuffer);
			return (bool)SteamAPI_ISteamRemoteStorage_FileReadAsyncComplete(self, hReadCall, ptr1, cubToRead); }, py::arg("hReadCall"), py::arg("pvBuffer"), py::arg("cubToRead"))
 		.def("FileForget", [](ISteamRemoteStorage* self, uintptr_t pchFile) { 
			const char * ptr0 = reinterpret_cast<const char *>(pchFile);
			return (bool)SteamAPI_ISteamRemoteStorage_FileForget(self, ptr0); }, py::arg("pchFile"))
 		.def("FileDelete", [](ISteamRemoteStorage* self, uintptr_t pchFile) { 
			const char * ptr0 = reinterpret_cast<const char *>(pchFile);
			return (bool)SteamAPI_ISteamRemoteStorage_FileDelete(self, ptr0); }, py::arg("pchFile"))
 		.def("FileShare", [](ISteamRemoteStorage* self, uintptr_t pchFile) { 
			const char * ptr0 = reinterpret_cast<const char *>(pchFile);
			return (SteamAPICall_t)SteamAPI_ISteamRemoteStorage_FileShare(self, ptr0); }, py::arg("pchFile"))
 		.def("SetSyncPlatforms", [](ISteamRemoteStorage* self, uintptr_t pchFile, ERemoteStoragePlatform eRemoteStoragePlatform) { 
			const char * ptr0 = reinterpret_cast<const char *>(pchFile);
			return (bool)SteamAPI_ISteamRemoteStorage_SetSyncPlatforms(self, ptr0, eRemoteStoragePlatform); }, py::arg("pchFile"), py::arg("eRemoteStoragePlatform"))
 		.def("FileWriteStreamOpen", [](ISteamRemoteStorage* self, uintptr_t pchFile) { 
			const char * ptr0 = reinterpret_cast<const char *>(pchFile);
			return (UGCFileWriteStreamHandle_t)SteamAPI_ISteamRemoteStorage_FileWriteStreamOpen(self, ptr0); }, py::arg("pchFile"))
 		.def("FileWriteStreamWriteChunk", [](ISteamRemoteStorage* self, UGCFileWriteStreamHandle_t writeHandle, uintptr_t pvData, int32 cubData) { 
			const void * ptr1 = reinterpret_cast<const void *>(pvData);
			return (bool)SteamAPI_ISteamRemoteStorage_FileWriteStreamWriteChunk(self, writeHandle, ptr1, cubData); }, py::arg("writeHandle"), py::arg("pvData"), py::arg("cubData"))
 		.def("FileWriteStreamClose", &SteamAPI_ISteamRemoteStorage_FileWriteStreamClose, py::arg("writeHandle"))
 		.def("FileWriteStreamCancel", &SteamAPI_ISteamRemoteStorage_FileWriteStreamCancel, py::arg("writeHandle"))
 		.def("FileExists", [](ISteamRemoteStorage* self, uintptr_t pchFile) { 
			const char * ptr0 = reinterpret_cast<const char *>(pchFile);
			return (bool)SteamAPI_ISteamRemoteStorage_FileExists(self, ptr0); }, py::arg("pchFile"))
 		.def("FilePersisted", [](ISteamRemoteStorage* self, uintptr_t pchFile) { 
			const char * ptr0 = reinterpret_cast<const char *>(pchFile);
			return (bool)SteamAPI_ISteamRemoteStorage_FilePersisted(self, ptr0); }, py::arg("pchFile"))
 		.def("GetFileSize", [](ISteamRemoteStorage* self, uintptr_t pchFile) { 
			const char * ptr0 = reinterpret_cast<const char *>(pchFile);
			return (int32)SteamAPI_ISteamRemoteStorage_GetFileSize(self, ptr0); }, py::arg("pchFile"))
 		.def("GetFileTimestamp", [](ISteamRemoteStorage* self, uintptr_t pchFile) { 
			const char * ptr0 = reinterpret_cast<const char *>(pchFile);
			return (int64)SteamAPI_ISteamRemoteStorage_GetFileTimestamp(self, ptr0); }, py::arg("pchFile"))
 		.def("GetSyncPlatforms", [](ISteamRemoteStorage* self, uintptr_t pchFile) { 
			const char * ptr0 = reinterpret_cast<const char *>(pchFile);
			return (ERemoteStoragePlatform)SteamAPI_ISteamRemoteStorage_GetSyncPlatforms(self, ptr0); }, py::arg("pchFile"))
 		.def("GetFileCount", &SteamAPI_ISteamRemoteStorage_GetFileCount)
 		.def("GetFileNameAndSize", [](ISteamRemoteStorage* self, int iFile, uintptr_t pnFileSizeInBytes) { 
			int32 * ptr1 = reinterpret_cast<int32 *>(pnFileSizeInBytes);
			return (const char *)SteamAPI_ISteamRemoteStorage_GetFileNameAndSize(self, iFile, ptr1); }, py::arg("iFile"), py::arg("pnFileSizeInBytes"))
 		.def("GetQuota", [](ISteamRemoteStorage* self, uintptr_t pnTotalBytes, uintptr_t puAvailableBytes) { 
			uint64 * ptr0 = reinterpret_cast<uint64 *>(pnTotalBytes);
			uint64 * ptr1 = reinterpret_cast<uint64 *>(puAvailableBytes);
			return (bool)SteamAPI_ISteamRemoteStorage_GetQuota(self, ptr0, ptr1); }, py::arg("pnTotalBytes"), py::arg("puAvailableBytes"))
 		.def("IsCloudEnabledForAccount", &SteamAPI_ISteamRemoteStorage_IsCloudEnabledForAccount)
 		.def("IsCloudEnabledForApp", &SteamAPI_ISteamRemoteStorage_IsCloudEnabledForApp)
 		.def("SetCloudEnabledForApp", &SteamAPI_ISteamRemoteStorage_SetCloudEnabledForApp, py::arg("bEnabled"))
 		.def("UGCDownload", &SteamAPI_ISteamRemoteStorage_UGCDownload, py::arg("hContent"), py::arg("unPriority"))
 		.def("GetUGCDownloadProgress", [](ISteamRemoteStorage* self, UGCHandle_t hContent, uintptr_t pnBytesDownloaded, uintptr_t pnBytesExpected) { 
			int32 * ptr1 = reinterpret_cast<int32 *>(pnBytesDownloaded);
			int32 * ptr2 = reinterpret_cast<int32 *>(pnBytesExpected);
			return (bool)SteamAPI_ISteamRemoteStorage_GetUGCDownloadProgress(self, hContent, ptr1, ptr2); }, py::arg("hContent"), py::arg("pnBytesDownloaded"), py::arg("pnBytesExpected"))
 		.def("GetUGCDetails", [](ISteamRemoteStorage* self, UGCHandle_t hContent, uintptr_t pnAppID, uintptr_t ppchName, uintptr_t pnFileSizeInBytes, uintptr_t pSteamIDOwner) { 
			AppId_t * ptr1 = reinterpret_cast<AppId_t *>(pnAppID);
			char ** ptr2 = reinterpret_cast<char **>(ppchName);
			int32 * ptr3 = reinterpret_cast<int32 *>(pnFileSizeInBytes);
			CSteamID * ptr4 = reinterpret_cast<CSteamID *>(pSteamIDOwner);
			return (bool)SteamAPI_ISteamRemoteStorage_GetUGCDetails(self, hContent, ptr1, ptr2, ptr3, ptr4); }, py::arg("hContent"), py::arg("pnAppID"), py::arg("ppchName"), py::arg("pnFileSizeInBytes"), py::arg("pSteamIDOwner"))
 		.def("UGCRead", [](ISteamRemoteStorage* self, UGCHandle_t hContent, uintptr_t pvData, int32 cubDataToRead, uint32 cOffset, EUGCReadAction eAction) { 
			void * ptr1 = reinterpret_cast<void *>(pvData);
			return (int32)SteamAPI_ISteamRemoteStorage_UGCRead(self, hContent, ptr1, cubDataToRead, cOffset, eAction); }, py::arg("hContent"), py::arg("pvData"), py::arg("cubDataToRead"), py::arg("cOffset"), py::arg("eAction"))
 		.def("GetCachedUGCCount", &SteamAPI_ISteamRemoteStorage_GetCachedUGCCount)
 		.def("GetCachedUGCHandle", &SteamAPI_ISteamRemoteStorage_GetCachedUGCHandle, py::arg("iCachedContent"))
 		.def("PublishWorkshopFile", [](ISteamRemoteStorage* self, uintptr_t pchFile, uintptr_t pchPreviewFile, AppId_t nConsumerAppId, uintptr_t pchTitle, uintptr_t pchDescription, ERemoteStoragePublishedFileVisibility eVisibility, uintptr_t pTags, EWorkshopFileType eWorkshopFileType) { 
			const char * ptr0 = reinterpret_cast<const char *>(pchFile);
			const char * ptr1 = reinterpret_cast<const char *>(pchPreviewFile);
			const char * ptr3 = reinterpret_cast<const char *>(pchTitle);
			const char * ptr4 = reinterpret_cast<const char *>(pchDescription);
			SteamParamStringArray_t * ptr6 = reinterpret_cast<SteamParamStringArray_t *>(pTags);
			return (SteamAPICall_t)SteamAPI_ISteamRemoteStorage_PublishWorkshopFile(self, ptr0, ptr1, nConsumerAppId, ptr3, ptr4, eVisibility, ptr6, eWorkshopFileType); }, py::arg("pchFile"), py::arg("pchPreviewFile"), py::arg("nConsumerAppId"), py::arg("pchTitle"), py::arg("pchDescription"), py::arg("eVisibility"), py::arg("pTags"), py::arg("eWorkshopFileType"))
 		.def("CreatePublishedFileUpdateRequest", &SteamAPI_ISteamRemoteStorage_CreatePublishedFileUpdateRequest, py::arg("unPublishedFileId"))
 		.def("UpdatePublishedFileFile", [](ISteamRemoteStorage* self, PublishedFileUpdateHandle_t updateHandle, uintptr_t pchFile) { 
			const char * ptr1 = reinterpret_cast<const char *>(pchFile);
			return (bool)SteamAPI_ISteamRemoteStorage_UpdatePublishedFileFile(self, updateHandle, ptr1); }, py::arg("updateHandle"), py::arg("pchFile"))
 		.def("UpdatePublishedFilePreviewFile", [](ISteamRemoteStorage* self, PublishedFileUpdateHandle_t updateHandle, uintptr_t pchPreviewFile) { 
			const char * ptr1 = reinterpret_cast<const char *>(pchPreviewFile);
			return (bool)SteamAPI_ISteamRemoteStorage_UpdatePublishedFilePreviewFile(self, updateHandle, ptr1); }, py::arg("updateHandle"), py::arg("pchPreviewFile"))
 		.def("UpdatePublishedFileTitle", [](ISteamRemoteStorage* self, PublishedFileUpdateHandle_t updateHandle, uintptr_t pchTitle) { 
			const char * ptr1 = reinterpret_cast<const char *>(pchTitle);
			return (bool)SteamAPI_ISteamRemoteStorage_UpdatePublishedFileTitle(self, updateHandle, ptr1); }, py::arg("updateHandle"), py::arg("pchTitle"))
 		.def("UpdatePublishedFileDescription", [](ISteamRemoteStorage* self, PublishedFileUpdateHandle_t updateHandle, uintptr_t pchDescription) { 
			const char * ptr1 = reinterpret_cast<const char *>(pchDescription);
			return (bool)SteamAPI_ISteamRemoteStorage_UpdatePublishedFileDescription(self, updateHandle, ptr1); }, py::arg("updateHandle"), py::arg("pchDescription"))
 		.def("UpdatePublishedFileVisibility", &SteamAPI_ISteamRemoteStorage_UpdatePublishedFileVisibility, py::arg("updateHandle"), py::arg("eVisibility"))
 		.def("UpdatePublishedFileTags", [](ISteamRemoteStorage* self, PublishedFileUpdateHandle_t updateHandle, uintptr_t pTags) { 
			SteamParamStringArray_t * ptr1 = reinterpret_cast<SteamParamStringArray_t *>(pTags);
			return (bool)SteamAPI_ISteamRemoteStorage_UpdatePublishedFileTags(self, updateHandle, ptr1); }, py::arg("updateHandle"), py::arg("pTags"))
 		.def("CommitPublishedFileUpdate", &SteamAPI_ISteamRemoteStorage_CommitPublishedFileUpdate, py::arg("updateHandle"))
 		.def("GetPublishedFileDetails", &SteamAPI_ISteamRemoteStorage_GetPublishedFileDetails, py::arg("unPublishedFileId"), py::arg("unMaxSecondsOld"))
 		.def("DeletePublishedFile", &SteamAPI_ISteamRemoteStorage_DeletePublishedFile, py::arg("unPublishedFileId"))
 		.def("EnumerateUserPublishedFiles", &SteamAPI_ISteamRemoteStorage_EnumerateUserPublishedFiles, py::arg("unStartIndex"))
 		.def("SubscribePublishedFile", &SteamAPI_ISteamRemoteStorage_SubscribePublishedFile, py::arg("unPublishedFileId"))
 		.def("EnumerateUserSubscribedFiles", &SteamAPI_ISteamRemoteStorage_EnumerateUserSubscribedFiles, py::arg("unStartIndex"))
 		.def("UnsubscribePublishedFile", &SteamAPI_ISteamRemoteStorage_UnsubscribePublishedFile, py::arg("unPublishedFileId"))
 		.def("UpdatePublishedFileSetChangeDescription", [](ISteamRemoteStorage* self, PublishedFileUpdateHandle_t updateHandle, uintptr_t pchChangeDescription) { 
			const char * ptr1 = reinterpret_cast<const char *>(pchChangeDescription);
			return (bool)SteamAPI_ISteamRemoteStorage_UpdatePublishedFileSetChangeDescription(self, updateHandle, ptr1); }, py::arg("updateHandle"), py::arg("pchChangeDescription"))
 		.def("GetPublishedItemVoteDetails", &SteamAPI_ISteamRemoteStorage_GetPublishedItemVoteDetails, py::arg("unPublishedFileId"))
 		.def("UpdateUserPublishedItemVote", &SteamAPI_ISteamRemoteStorage_UpdateUserPublishedItemVote, py::arg("unPublishedFileId"), py::arg("bVoteUp"))
 		.def("GetUserPublishedItemVoteDetails", &SteamAPI_ISteamRemoteStorage_GetUserPublishedItemVoteDetails, py::arg("unPublishedFileId"))
 		.def("EnumerateUserSharedWorkshopFiles", [](ISteamRemoteStorage* self, uint64_steamid steamId, uint32 unStartIndex, uintptr_t pRequiredTags, uintptr_t pExcludedTags) { 
			SteamParamStringArray_t * ptr2 = reinterpret_cast<SteamParamStringArray_t *>(pRequiredTags);
			SteamParamStringArray_t * ptr3 = reinterpret_cast<SteamParamStringArray_t *>(pExcludedTags);
			return (SteamAPICall_t)SteamAPI_ISteamRemoteStorage_EnumerateUserSharedWorkshopFiles(self, steamId, unStartIndex, ptr2, ptr3); }, py::arg("steamId"), py::arg("unStartIndex"), py::arg("pRequiredTags"), py::arg("pExcludedTags"))
 		.def("PublishVideo", [](ISteamRemoteStorage* self, EWorkshopVideoProvider eVideoProvider, uintptr_t pchVideoAccount, uintptr_t pchVideoIdentifier, uintptr_t pchPreviewFile, AppId_t nConsumerAppId, uintptr_t pchTitle, uintptr_t pchDescription, ERemoteStoragePublishedFileVisibility eVisibility, uintptr_t pTags) { 
			const char * ptr1 = reinterpret_cast<const char *>(pchVideoAccount);
			const char * ptr2 = reinterpret_cast<const char *>(pchVideoIdentifier);
			const char * ptr3 = reinterpret_cast<const char *>(pchPreviewFile);
			const char * ptr5 = reinterpret_cast<const char *>(pchTitle);
			const char * ptr6 = reinterpret_cast<const char *>(pchDescription);
			SteamParamStringArray_t * ptr8 = reinterpret_cast<SteamParamStringArray_t *>(pTags);
			return (SteamAPICall_t)SteamAPI_ISteamRemoteStorage_PublishVideo(self, eVideoProvider, ptr1, ptr2, ptr3, nConsumerAppId, ptr5, ptr6, eVisibility, ptr8); }, py::arg("eVideoProvider"), py::arg("pchVideoAccount"), py::arg("pchVideoIdentifier"), py::arg("pchPreviewFile"), py::arg("nConsumerAppId"), py::arg("pchTitle"), py::arg("pchDescription"), py::arg("eVisibility"), py::arg("pTags"))
 		.def("SetUserPublishedFileAction", &SteamAPI_ISteamRemoteStorage_SetUserPublishedFileAction, py::arg("unPublishedFileId"), py::arg("eAction"))
 		.def("EnumeratePublishedFilesByUserAction", &SteamAPI_ISteamRemoteStorage_EnumeratePublishedFilesByUserAction, py::arg("eAction"), py::arg("unStartIndex"))
 		.def("EnumeratePublishedWorkshopFiles", [](ISteamRemoteStorage* self, EWorkshopEnumerationType eEnumerationType, uint32 unStartIndex, uint32 unCount, uint32 unDays, uintptr_t pTags, uintptr_t pUserTags) { 
			SteamParamStringArray_t * ptr4 = reinterpret_cast<SteamParamStringArray_t *>(pTags);
			SteamParamStringArray_t * ptr5 = reinterpret_cast<SteamParamStringArray_t *>(pUserTags);
			return (SteamAPICall_t)SteamAPI_ISteamRemoteStorage_EnumeratePublishedWorkshopFiles(self, eEnumerationType, unStartIndex, unCount, unDays, ptr4, ptr5); }, py::arg("eEnumerationType"), py::arg("unStartIndex"), py::arg("unCount"), py::arg("unDays"), py::arg("pTags"), py::arg("pUserTags"))
 		.def("UGCDownloadToLocation", [](ISteamRemoteStorage* self, UGCHandle_t hContent, uintptr_t pchLocation, uint32 unPriority) { 
			const char * ptr1 = reinterpret_cast<const char *>(pchLocation);
			return (SteamAPICall_t)SteamAPI_ISteamRemoteStorage_UGCDownloadToLocation(self, hContent, ptr1, unPriority); }, py::arg("hContent"), py::arg("pchLocation"), py::arg("unPriority"))
 		.def("GetLocalFileChangeCount", &SteamAPI_ISteamRemoteStorage_GetLocalFileChangeCount)
 		.def("GetLocalFileChange", [](ISteamRemoteStorage* self, int iFile, uintptr_t pEChangeType, uintptr_t pEFilePathType) { 
			ERemoteStorageLocalFileChange * ptr1 = reinterpret_cast<ERemoteStorageLocalFileChange *>(pEChangeType);
			ERemoteStorageFilePathType * ptr2 = reinterpret_cast<ERemoteStorageFilePathType *>(pEFilePathType);
			return (const char *)SteamAPI_ISteamRemoteStorage_GetLocalFileChange(self, iFile, ptr1, ptr2); }, py::arg("iFile"), py::arg("pEChangeType"), py::arg("pEFilePathType"))
 		.def("BeginFileWriteBatch", &SteamAPI_ISteamRemoteStorage_BeginFileWriteBatch)
 		.def("EndFileWriteBatch", &SteamAPI_ISteamRemoteStorage_EndFileWriteBatch)
 	;
 	m.def("SteamUserStats",[]() -> ISteamUserStats* { return SteamUserStats(); }, py::return_value_policy::reference);
 	py::class_<ISteamUserStats, std::unique_ptr<ISteamUserStats, py::nodelete>> _isteamuserstats(m, "ISteamUserStats");
 	_isteamuserstats
 		.def_property_readonly("ptr", [](ISteamUserStats& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def("GetStat", [](ISteamUserStats* self, uintptr_t pchName, uintptr_t pData) { 
			const char * ptr0 = reinterpret_cast<const char *>(pchName);
			float * ptr1 = reinterpret_cast<float *>(pData);
			return (bool)SteamAPI_ISteamUserStats_GetStatFloat(self, ptr0, ptr1); }, py::arg("pchName"), py::arg("pData"))
 		.def("SetStat", [](ISteamUserStats* self, uintptr_t pchName, float fData) { 
			const char * ptr0 = reinterpret_cast<const char *>(pchName);
			return (bool)SteamAPI_ISteamUserStats_SetStatFloat(self, ptr0, fData); }, py::arg("pchName"), py::arg("fData"))
 		.def("UpdateAvgRateStat", [](ISteamUserStats* self, uintptr_t pchName, float flCountThisSession, double dSessionLength) { 
			const char * ptr0 = reinterpret_cast<const char *>(pchName);
			return (bool)SteamAPI_ISteamUserStats_UpdateAvgRateStat(self, ptr0, flCountThisSession, dSessionLength); }, py::arg("pchName"), py::arg("flCountThisSession"), py::arg("dSessionLength"))
 		.def("GetAchievement", [](ISteamUserStats* self, uintptr_t pchName, uintptr_t pbAchieved) { 
			const char * ptr0 = reinterpret_cast<const char *>(pchName);
			bool * ptr1 = reinterpret_cast<bool *>(pbAchieved);
			return (bool)SteamAPI_ISteamUserStats_GetAchievement(self, ptr0, ptr1); }, py::arg("pchName"), py::arg("pbAchieved"))
 		.def("SetAchievement", [](ISteamUserStats* self, uintptr_t pchName) { 
			const char * ptr0 = reinterpret_cast<const char *>(pchName);
			return (bool)SteamAPI_ISteamUserStats_SetAchievement(self, ptr0); }, py::arg("pchName"))
 		.def("ClearAchievement", [](ISteamUserStats* self, uintptr_t pchName) { 
			const char * ptr0 = reinterpret_cast<const char *>(pchName);
			return (bool)SteamAPI_ISteamUserStats_ClearAchievement(self, ptr0); }, py::arg("pchName"))
 		.def("GetAchievementAndUnlockTime", [](ISteamUserStats* self, uintptr_t pchName, uintptr_t pbAchieved, uintptr_t punUnlockTime) { 
			const char * ptr0 = reinterpret_cast<const char *>(pchName);
			bool * ptr1 = reinterpret_cast<bool *>(pbAchieved);
			uint32 * ptr2 = reinterpret_cast<uint32 *>(punUnlockTime);
			return (bool)SteamAPI_ISteamUserStats_GetAchievementAndUnlockTime(self, ptr0, ptr1, ptr2); }, py::arg("pchName"), py::arg("pbAchieved"), py::arg("punUnlockTime"))
 		.def("StoreStats", &SteamAPI_ISteamUserStats_StoreStats)
 		.def("GetAchievementIcon", [](ISteamUserStats* self, uintptr_t pchName) { 
			const char * ptr0 = reinterpret_cast<const char *>(pchName);
			return (int)SteamAPI_ISteamUserStats_GetAchievementIcon(self, ptr0); }, py::arg("pchName"))
 		.def("GetAchievementDisplayAttribute", [](ISteamUserStats* self, uintptr_t pchName, uintptr_t pchKey) { 
			const char * ptr0 = reinterpret_cast<const char *>(pchName);
			const char * ptr1 = reinterpret_cast<const char *>(pchKey);
			return (const char *)SteamAPI_ISteamUserStats_GetAchievementDisplayAttribute(self, ptr0, ptr1); }, py::arg("pchName"), py::arg("pchKey"))
 		.def("IndicateAchievementProgress", [](ISteamUserStats* self, uintptr_t pchName, uint32 nCurProgress, uint32 nMaxProgress) { 
			const char * ptr0 = reinterpret_cast<const char *>(pchName);
			return (bool)SteamAPI_ISteamUserStats_IndicateAchievementProgress(self, ptr0, nCurProgress, nMaxProgress); }, py::arg("pchName"), py::arg("nCurProgress"), py::arg("nMaxProgress"))
 		.def("GetNumAchievements", &SteamAPI_ISteamUserStats_GetNumAchievements)
 		.def("GetAchievementName", &SteamAPI_ISteamUserStats_GetAchievementName, py::arg("iAchievement"))
 		.def("RequestUserStats", &SteamAPI_ISteamUserStats_RequestUserStats, py::arg("steamIDUser"))
 		.def("GetUserStat", [](ISteamUserStats* self, uint64_steamid steamIDUser, uintptr_t pchName, uintptr_t pData) { 
			const char * ptr1 = reinterpret_cast<const char *>(pchName);
			float * ptr2 = reinterpret_cast<float *>(pData);
			return (bool)SteamAPI_ISteamUserStats_GetUserStatFloat(self, steamIDUser, ptr1, ptr2); }, py::arg("steamIDUser"), py::arg("pchName"), py::arg("pData"))
 		.def("GetUserAchievement", [](ISteamUserStats* self, uint64_steamid steamIDUser, uintptr_t pchName, uintptr_t pbAchieved) { 
			const char * ptr1 = reinterpret_cast<const char *>(pchName);
			bool * ptr2 = reinterpret_cast<bool *>(pbAchieved);
			return (bool)SteamAPI_ISteamUserStats_GetUserAchievement(self, steamIDUser, ptr1, ptr2); }, py::arg("steamIDUser"), py::arg("pchName"), py::arg("pbAchieved"))
 		.def("GetUserAchievementAndUnlockTime", [](ISteamUserStats* self, uint64_steamid steamIDUser, uintptr_t pchName, uintptr_t pbAchieved, uintptr_t punUnlockTime) { 
			const char * ptr1 = reinterpret_cast<const char *>(pchName);
			bool * ptr2 = reinterpret_cast<bool *>(pbAchieved);
			uint32 * ptr3 = reinterpret_cast<uint32 *>(punUnlockTime);
			return (bool)SteamAPI_ISteamUserStats_GetUserAchievementAndUnlockTime(self, steamIDUser, ptr1, ptr2, ptr3); }, py::arg("steamIDUser"), py::arg("pchName"), py::arg("pbAchieved"), py::arg("punUnlockTime"))
 		.def("ResetAllStats", &SteamAPI_ISteamUserStats_ResetAllStats, py::arg("bAchievementsToo"))
 		.def("FindOrCreateLeaderboard", [](ISteamUserStats* self, uintptr_t pchLeaderboardName, ELeaderboardSortMethod eLeaderboardSortMethod, ELeaderboardDisplayType eLeaderboardDisplayType) { 
			const char * ptr0 = reinterpret_cast<const char *>(pchLeaderboardName);
			return (SteamAPICall_t)SteamAPI_ISteamUserStats_FindOrCreateLeaderboard(self, ptr0, eLeaderboardSortMethod, eLeaderboardDisplayType); }, py::arg("pchLeaderboardName"), py::arg("eLeaderboardSortMethod"), py::arg("eLeaderboardDisplayType"))
 		.def("FindLeaderboard", [](ISteamUserStats* self, uintptr_t pchLeaderboardName) { 
			const char * ptr0 = reinterpret_cast<const char *>(pchLeaderboardName);
			return (SteamAPICall_t)SteamAPI_ISteamUserStats_FindLeaderboard(self, ptr0); }, py::arg("pchLeaderboardName"))
 		.def("GetLeaderboardName", &SteamAPI_ISteamUserStats_GetLeaderboardName, py::arg("hSteamLeaderboard"))
 		.def("GetLeaderboardEntryCount", &SteamAPI_ISteamUserStats_GetLeaderboardEntryCount, py::arg("hSteamLeaderboard"))
 		.def("GetLeaderboardSortMethod", &SteamAPI_ISteamUserStats_GetLeaderboardSortMethod, py::arg("hSteamLeaderboard"))
 		.def("GetLeaderboardDisplayType", &SteamAPI_ISteamUserStats_GetLeaderboardDisplayType, py::arg("hSteamLeaderboard"))
 		.def("DownloadLeaderboardEntries", &SteamAPI_ISteamUserStats_DownloadLeaderboardEntries, py::arg("hSteamLeaderboard"), py::arg("eLeaderboardDataRequest"), py::arg("nRangeStart"), py::arg("nRangeEnd"))
 		.def("DownloadLeaderboardEntriesForUsers", [](ISteamUserStats* self, SteamLeaderboard_t hSteamLeaderboard, uintptr_t prgUsers, int cUsers) { 
			CSteamID * ptr1 = reinterpret_cast<CSteamID *>(prgUsers);
			return (SteamAPICall_t)SteamAPI_ISteamUserStats_DownloadLeaderboardEntriesForUsers(self, hSteamLeaderboard, ptr1, cUsers); }, py::arg("hSteamLeaderboard"), py::arg("prgUsers"), py::arg("cUsers"))
 		.def("GetDownloadedLeaderboardEntry", [](ISteamUserStats* self, SteamLeaderboardEntries_t hSteamLeaderboardEntries, int index, uintptr_t pLeaderboardEntry, uintptr_t pDetails, int cDetailsMax) { 
			LeaderboardEntry_t * ptr2 = reinterpret_cast<LeaderboardEntry_t *>(pLeaderboardEntry);
			int32 * ptr3 = reinterpret_cast<int32 *>(pDetails);
			return (bool)SteamAPI_ISteamUserStats_GetDownloadedLeaderboardEntry(self, hSteamLeaderboardEntries, index, ptr2, ptr3, cDetailsMax); }, py::arg("hSteamLeaderboardEntries"), py::arg("index"), py::arg("pLeaderboardEntry"), py::arg("pDetails"), py::arg("cDetailsMax"))
 		.def("UploadLeaderboardScore", [](ISteamUserStats* self, SteamLeaderboard_t hSteamLeaderboard, ELeaderboardUploadScoreMethod eLeaderboardUploadScoreMethod, int32 nScore, uintptr_t pScoreDetails, int cScoreDetailsCount) { 
			const int32 * ptr3 = reinterpret_cast<const int32 *>(pScoreDetails);
			return (SteamAPICall_t)SteamAPI_ISteamUserStats_UploadLeaderboardScore(self, hSteamLeaderboard, eLeaderboardUploadScoreMethod, nScore, ptr3, cScoreDetailsCount); }, py::arg("hSteamLeaderboard"), py::arg("eLeaderboardUploadScoreMethod"), py::arg("nScore"), py::arg("pScoreDetails"), py::arg("cScoreDetailsCount"))
 		.def("AttachLeaderboardUGC", &SteamAPI_ISteamUserStats_AttachLeaderboardUGC, py::arg("hSteamLeaderboard"), py::arg("hUGC"))
 		.def("GetNumberOfCurrentPlayers", &SteamAPI_ISteamUserStats_GetNumberOfCurrentPlayers)
 		.def("RequestGlobalAchievementPercentages", &SteamAPI_ISteamUserStats_RequestGlobalAchievementPercentages)
 		.def("GetMostAchievedAchievementInfo", [](ISteamUserStats* self, uintptr_t pchName, uint32 unNameBufLen, uintptr_t pflPercent, uintptr_t pbAchieved) { 
			char * ptr0 = reinterpret_cast<char *>(pchName);
			float * ptr2 = reinterpret_cast<float *>(pflPercent);
			bool * ptr3 = reinterpret_cast<bool *>(pbAchieved);
			return (int)SteamAPI_ISteamUserStats_GetMostAchievedAchievementInfo(self, ptr0, unNameBufLen, ptr2, ptr3); }, py::arg("pchName"), py::arg("unNameBufLen"), py::arg("pflPercent"), py::arg("pbAchieved"))
 		.def("GetNextMostAchievedAchievementInfo", [](ISteamUserStats* self, int iIteratorPrevious, uintptr_t pchName, uint32 unNameBufLen, uintptr_t pflPercent, uintptr_t pbAchieved) { 
			char * ptr1 = reinterpret_cast<char *>(pchName);
			float * ptr3 = reinterpret_cast<float *>(pflPercent);
			bool * ptr4 = reinterpret_cast<bool *>(pbAchieved);
			return (int)SteamAPI_ISteamUserStats_GetNextMostAchievedAchievementInfo(self, iIteratorPrevious, ptr1, unNameBufLen, ptr3, ptr4); }, py::arg("iIteratorPrevious"), py::arg("pchName"), py::arg("unNameBufLen"), py::arg("pflPercent"), py::arg("pbAchieved"))
 		.def("GetAchievementAchievedPercent", [](ISteamUserStats* self, uintptr_t pchName, uintptr_t pflPercent) { 
			const char * ptr0 = reinterpret_cast<const char *>(pchName);
			float * ptr1 = reinterpret_cast<float *>(pflPercent);
			return (bool)SteamAPI_ISteamUserStats_GetAchievementAchievedPercent(self, ptr0, ptr1); }, py::arg("pchName"), py::arg("pflPercent"))
 		.def("RequestGlobalStats", &SteamAPI_ISteamUserStats_RequestGlobalStats, py::arg("nHistoryDays"))
 		.def("GetGlobalStat", [](ISteamUserStats* self, uintptr_t pchStatName, uintptr_t pData) { 
			const char * ptr0 = reinterpret_cast<const char *>(pchStatName);
			double * ptr1 = reinterpret_cast<double *>(pData);
			return (bool)SteamAPI_ISteamUserStats_GetGlobalStatDouble(self, ptr0, ptr1); }, py::arg("pchStatName"), py::arg("pData"))
 		.def("GetGlobalStatHistory", [](ISteamUserStats* self, uintptr_t pchStatName, uintptr_t pData, uint32 cubData) { 
			const char * ptr0 = reinterpret_cast<const char *>(pchStatName);
			double * ptr1 = reinterpret_cast<double *>(pData);
			return (int32)SteamAPI_ISteamUserStats_GetGlobalStatHistoryDouble(self, ptr0, ptr1, cubData); }, py::arg("pchStatName"), py::arg("pData"), py::arg("cubData"))
 		.def("GetAchievementProgressLimits", [](ISteamUserStats* self, uintptr_t pchName, uintptr_t pfMinProgress, uintptr_t pfMaxProgress) { 
			const char * ptr0 = reinterpret_cast<const char *>(pchName);
			float * ptr1 = reinterpret_cast<float *>(pfMinProgress);
			float * ptr2 = reinterpret_cast<float *>(pfMaxProgress);
			return (bool)SteamAPI_ISteamUserStats_GetAchievementProgressLimitsFloat(self, ptr0, ptr1, ptr2); }, py::arg("pchName"), py::arg("pfMinProgress"), py::arg("pfMaxProgress"))
 	;
 	m.def("SteamApps",[]() -> ISteamApps* { return SteamApps(); }, py::return_value_policy::reference);
 	py::class_<ISteamApps, std::unique_ptr<ISteamApps, py::nodelete>> _isteamapps(m, "ISteamApps");
 	_isteamapps
 		.def_property_readonly("ptr", [](ISteamApps& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def("BIsSubscribed", &SteamAPI_ISteamApps_BIsSubscribed)
 		.def("BIsLowViolence", &SteamAPI_ISteamApps_BIsLowViolence)
 		.def("BIsCybercafe", &SteamAPI_ISteamApps_BIsCybercafe)
 		.def("BIsVACBanned", &SteamAPI_ISteamApps_BIsVACBanned)
 		.def("GetCurrentGameLanguage", &SteamAPI_ISteamApps_GetCurrentGameLanguage)
 		.def("GetAvailableGameLanguages", &SteamAPI_ISteamApps_GetAvailableGameLanguages)
 		.def("BIsSubscribedApp", &SteamAPI_ISteamApps_BIsSubscribedApp, py::arg("appID"))
 		.def("BIsDlcInstalled", &SteamAPI_ISteamApps_BIsDlcInstalled, py::arg("appID"))
 		.def("GetEarliestPurchaseUnixTime", &SteamAPI_ISteamApps_GetEarliestPurchaseUnixTime, py::arg("nAppID"))
 		.def("BIsSubscribedFromFreeWeekend", &SteamAPI_ISteamApps_BIsSubscribedFromFreeWeekend)
 		.def("GetDLCCount", &SteamAPI_ISteamApps_GetDLCCount)
 		.def("BGetDLCDataByIndex", [](ISteamApps* self, int iDLC, uintptr_t pAppID, uintptr_t pbAvailable, uintptr_t pchName, int cchNameBufferSize) { 
			AppId_t * ptr1 = reinterpret_cast<AppId_t *>(pAppID);
			bool * ptr2 = reinterpret_cast<bool *>(pbAvailable);
			char * ptr3 = reinterpret_cast<char *>(pchName);
			return (bool)SteamAPI_ISteamApps_BGetDLCDataByIndex(self, iDLC, ptr1, ptr2, ptr3, cchNameBufferSize); }, py::arg("iDLC"), py::arg("pAppID"), py::arg("pbAvailable"), py::arg("pchName"), py::arg("cchNameBufferSize"))
 		.def("InstallDLC", &SteamAPI_ISteamApps_InstallDLC, py::arg("nAppID"))
 		.def("UninstallDLC", &SteamAPI_ISteamApps_UninstallDLC, py::arg("nAppID"))
 		.def("RequestAppProofOfPurchaseKey", &SteamAPI_ISteamApps_RequestAppProofOfPurchaseKey, py::arg("nAppID"))
 		.def("GetCurrentBetaName", [](ISteamApps* self, uintptr_t pchName, int cchNameBufferSize) { 
			char * ptr0 = reinterpret_cast<char *>(pchName);
			return (bool)SteamAPI_ISteamApps_GetCurrentBetaName(self, ptr0, cchNameBufferSize); }, py::arg("pchName"), py::arg("cchNameBufferSize"))
 		.def("MarkContentCorrupt", &SteamAPI_ISteamApps_MarkContentCorrupt, py::arg("bMissingFilesOnly"))
 		.def("GetInstalledDepots", [](ISteamApps* self, AppId_t appID, uintptr_t pvecDepots, uint32 cMaxDepots) { 
			DepotId_t * ptr1 = reinterpret_cast<DepotId_t *>(pvecDepots);
			return (uint32)SteamAPI_ISteamApps_GetInstalledDepots(self, appID, ptr1, cMaxDepots); }, py::arg("appID"), py::arg("pvecDepots"), py::arg("cMaxDepots"))
 		.def("GetAppInstallDir", [](ISteamApps* self, AppId_t appID, uintptr_t pchFolder, uint32 cchFolderBufferSize) { 
			char * ptr1 = reinterpret_cast<char *>(pchFolder);
			return (uint32)SteamAPI_ISteamApps_GetAppInstallDir(self, appID, ptr1, cchFolderBufferSize); }, py::arg("appID"), py::arg("pchFolder"), py::arg("cchFolderBufferSize"))
 		.def("BIsAppInstalled", &SteamAPI_ISteamApps_BIsAppInstalled, py::arg("appID"))
 		.def("GetAppOwner", &SteamAPI_ISteamApps_GetAppOwner)
 		.def("GetLaunchQueryParam", [](ISteamApps* self, uintptr_t pchKey) { 
			const char * ptr0 = reinterpret_cast<const char *>(pchKey);
			return (const char *)SteamAPI_ISteamApps_GetLaunchQueryParam(self, ptr0); }, py::arg("pchKey"))
 		.def("GetDlcDownloadProgress", [](ISteamApps* self, AppId_t nAppID, uintptr_t punBytesDownloaded, uintptr_t punBytesTotal) { 
			uint64 * ptr1 = reinterpret_cast<uint64 *>(punBytesDownloaded);
			uint64 * ptr2 = reinterpret_cast<uint64 *>(punBytesTotal);
			return (bool)SteamAPI_ISteamApps_GetDlcDownloadProgress(self, nAppID, ptr1, ptr2); }, py::arg("nAppID"), py::arg("punBytesDownloaded"), py::arg("punBytesTotal"))
 		.def("GetAppBuildId", &SteamAPI_ISteamApps_GetAppBuildId)
 		.def("RequestAllProofOfPurchaseKeys", &SteamAPI_ISteamApps_RequestAllProofOfPurchaseKeys)
 		.def("GetFileDetails", [](ISteamApps* self, uintptr_t pszFileName) { 
			const char * ptr0 = reinterpret_cast<const char *>(pszFileName);
			return (SteamAPICall_t)SteamAPI_ISteamApps_GetFileDetails(self, ptr0); }, py::arg("pszFileName"))
 		.def("GetLaunchCommandLine", [](ISteamApps* self, uintptr_t pszCommandLine, int cubCommandLine) { 
			char * ptr0 = reinterpret_cast<char *>(pszCommandLine);
			return (int)SteamAPI_ISteamApps_GetLaunchCommandLine(self, ptr0, cubCommandLine); }, py::arg("pszCommandLine"), py::arg("cubCommandLine"))
 		.def("BIsSubscribedFromFamilySharing", &SteamAPI_ISteamApps_BIsSubscribedFromFamilySharing)
 		.def("BIsTimedTrial", [](ISteamApps* self, uintptr_t punSecondsAllowed, uintptr_t punSecondsPlayed) { 
			uint32 * ptr0 = reinterpret_cast<uint32 *>(punSecondsAllowed);
			uint32 * ptr1 = reinterpret_cast<uint32 *>(punSecondsPlayed);
			return (bool)SteamAPI_ISteamApps_BIsTimedTrial(self, ptr0, ptr1); }, py::arg("punSecondsAllowed"), py::arg("punSecondsPlayed"))
 		.def("SetDlcContext", &SteamAPI_ISteamApps_SetDlcContext, py::arg("nAppID"))
 		.def("GetNumBetas", [](ISteamApps* self, uintptr_t pnAvailable, uintptr_t pnPrivate) { 
			int * ptr0 = reinterpret_cast<int *>(pnAvailable);
			int * ptr1 = reinterpret_cast<int *>(pnPrivate);
			return (int)SteamAPI_ISteamApps_GetNumBetas(self, ptr0, ptr1); }, py::arg("pnAvailable"), py::arg("pnPrivate"))
 		.def("GetBetaInfo", [](ISteamApps* self, int iBetaIndex, uintptr_t punFlags, uintptr_t punBuildID, uintptr_t pchBetaName, int cchBetaName, uintptr_t pchDescription, int cchDescription, uintptr_t punLastUpdated) { 
			uint32 * ptr1 = reinterpret_cast<uint32 *>(punFlags);
			uint32 * ptr2 = reinterpret_cast<uint32 *>(punBuildID);
			char * ptr3 = reinterpret_cast<char *>(pchBetaName);
			char * ptr5 = reinterpret_cast<char *>(pchDescription);
			uint32 * ptr7 = reinterpret_cast<uint32 *>(punLastUpdated);
			return (bool)SteamAPI_ISteamApps_GetBetaInfo(self, iBetaIndex, ptr1, ptr2, ptr3, cchBetaName, ptr5, cchDescription, ptr7); }, py::arg("iBetaIndex"), py::arg("punFlags"), py::arg("punBuildID"), py::arg("pchBetaName"), py::arg("cchBetaName"), py::arg("pchDescription"), py::arg("cchDescription"), py::arg("punLastUpdated"))
 		.def("SetActiveBeta", [](ISteamApps* self, uintptr_t pchBetaName) { 
			const char * ptr0 = reinterpret_cast<const char *>(pchBetaName);
			return (bool)SteamAPI_ISteamApps_SetActiveBeta(self, ptr0); }, py::arg("pchBetaName"))
 	;
 	m.def("SteamNetworking",[]() -> ISteamNetworking* { return SteamNetworking(); }, py::return_value_policy::reference);
 	py::class_<ISteamNetworking, std::unique_ptr<ISteamNetworking, py::nodelete>> _isteamnetworking(m, "ISteamNetworking");
 	_isteamnetworking
 		.def_property_readonly("ptr", [](ISteamNetworking& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def("SendP2PPacket", [](ISteamNetworking* self, uint64_steamid steamIDRemote, uintptr_t pubData, uint32 cubData, EP2PSend eP2PSendType, int nChannel) { 
			const void * ptr1 = reinterpret_cast<const void *>(pubData);
			return (bool)SteamAPI_ISteamNetworking_SendP2PPacket(self, steamIDRemote, ptr1, cubData, eP2PSendType, nChannel); }, py::arg("steamIDRemote"), py::arg("pubData"), py::arg("cubData"), py::arg("eP2PSendType"), py::arg("nChannel"))
 		.def("IsP2PPacketAvailable", [](ISteamNetworking* self, uintptr_t pcubMsgSize, int nChannel) { 
			uint32 * ptr0 = reinterpret_cast<uint32 *>(pcubMsgSize);
			return (bool)SteamAPI_ISteamNetworking_IsP2PPacketAvailable(self, ptr0, nChannel); }, py::arg("pcubMsgSize"), py::arg("nChannel"))
 		.def("ReadP2PPacket", [](ISteamNetworking* self, uintptr_t pubDest, uint32 cubDest, uintptr_t pcubMsgSize, uintptr_t psteamIDRemote, int nChannel) { 
			void * ptr0 = reinterpret_cast<void *>(pubDest);
			uint32 * ptr2 = reinterpret_cast<uint32 *>(pcubMsgSize);
			CSteamID * ptr3 = reinterpret_cast<CSteamID *>(psteamIDRemote);
			return (bool)SteamAPI_ISteamNetworking_ReadP2PPacket(self, ptr0, cubDest, ptr2, ptr3, nChannel); }, py::arg("pubDest"), py::arg("cubDest"), py::arg("pcubMsgSize"), py::arg("psteamIDRemote"), py::arg("nChannel"))
 		.def("AcceptP2PSessionWithUser", &SteamAPI_ISteamNetworking_AcceptP2PSessionWithUser, py::arg("steamIDRemote"))
 		.def("CloseP2PSessionWithUser", &SteamAPI_ISteamNetworking_CloseP2PSessionWithUser, py::arg("steamIDRemote"))
 		.def("CloseP2PChannelWithUser", &SteamAPI_ISteamNetworking_CloseP2PChannelWithUser, py::arg("steamIDRemote"), py::arg("nChannel"))
 		.def("GetP2PSessionState", [](ISteamNetworking* self, uint64_steamid steamIDRemote, uintptr_t pConnectionState) { 
			P2PSessionState_t * ptr1 = reinterpret_cast<P2PSessionState_t *>(pConnectionState);
			return (bool)SteamAPI_ISteamNetworking_GetP2PSessionState(self, steamIDRemote, ptr1); }, py::arg("steamIDRemote"), py::arg("pConnectionState"))
 		.def("AllowP2PPacketRelay", &SteamAPI_ISteamNetworking_AllowP2PPacketRelay, py::arg("bAllow"))
 		.def("CreateListenSocket", &SteamAPI_ISteamNetworking_CreateListenSocket, py::arg("nVirtualP2PPort"), py::arg("nIP"), py::arg("nPort"), py::arg("bAllowUseOfPacketRelay"))
 		.def("CreateP2PConnectionSocket", &SteamAPI_ISteamNetworking_CreateP2PConnectionSocket, py::arg("steamIDTarget"), py::arg("nVirtualPort"), py::arg("nTimeoutSec"), py::arg("bAllowUseOfPacketRelay"))
 		.def("CreateConnectionSocket", &SteamAPI_ISteamNetworking_CreateConnectionSocket, py::arg("nIP"), py::arg("nPort"), py::arg("nTimeoutSec"))
 		.def("DestroySocket", &SteamAPI_ISteamNetworking_DestroySocket, py::arg("hSocket"), py::arg("bNotifyRemoteEnd"))
 		.def("DestroyListenSocket", &SteamAPI_ISteamNetworking_DestroyListenSocket, py::arg("hSocket"), py::arg("bNotifyRemoteEnd"))
 		.def("SendDataOnSocket", [](ISteamNetworking* self, SNetSocket_t hSocket, uintptr_t pubData, uint32 cubData, bool bReliable) { 
			void * ptr1 = reinterpret_cast<void *>(pubData);
			return (bool)SteamAPI_ISteamNetworking_SendDataOnSocket(self, hSocket, ptr1, cubData, bReliable); }, py::arg("hSocket"), py::arg("pubData"), py::arg("cubData"), py::arg("bReliable"))
 		.def("IsDataAvailableOnSocket", [](ISteamNetworking* self, SNetSocket_t hSocket, uintptr_t pcubMsgSize) { 
			uint32 * ptr1 = reinterpret_cast<uint32 *>(pcubMsgSize);
			return (bool)SteamAPI_ISteamNetworking_IsDataAvailableOnSocket(self, hSocket, ptr1); }, py::arg("hSocket"), py::arg("pcubMsgSize"))
 		.def("RetrieveDataFromSocket", [](ISteamNetworking* self, SNetSocket_t hSocket, uintptr_t pubDest, uint32 cubDest, uintptr_t pcubMsgSize) { 
			void * ptr1 = reinterpret_cast<void *>(pubDest);
			uint32 * ptr3 = reinterpret_cast<uint32 *>(pcubMsgSize);
			return (bool)SteamAPI_ISteamNetworking_RetrieveDataFromSocket(self, hSocket, ptr1, cubDest, ptr3); }, py::arg("hSocket"), py::arg("pubDest"), py::arg("cubDest"), py::arg("pcubMsgSize"))
 		.def("IsDataAvailable", [](ISteamNetworking* self, SNetListenSocket_t hListenSocket, uintptr_t pcubMsgSize, uintptr_t phSocket) { 
			uint32 * ptr1 = reinterpret_cast<uint32 *>(pcubMsgSize);
			SNetSocket_t * ptr2 = reinterpret_cast<SNetSocket_t *>(phSocket);
			return (bool)SteamAPI_ISteamNetworking_IsDataAvailable(self, hListenSocket, ptr1, ptr2); }, py::arg("hListenSocket"), py::arg("pcubMsgSize"), py::arg("phSocket"))
 		.def("RetrieveData", [](ISteamNetworking* self, SNetListenSocket_t hListenSocket, uintptr_t pubDest, uint32 cubDest, uintptr_t pcubMsgSize, uintptr_t phSocket) { 
			void * ptr1 = reinterpret_cast<void *>(pubDest);
			uint32 * ptr3 = reinterpret_cast<uint32 *>(pcubMsgSize);
			SNetSocket_t * ptr4 = reinterpret_cast<SNetSocket_t *>(phSocket);
			return (bool)SteamAPI_ISteamNetworking_RetrieveData(self, hListenSocket, ptr1, cubDest, ptr3, ptr4); }, py::arg("hListenSocket"), py::arg("pubDest"), py::arg("cubDest"), py::arg("pcubMsgSize"), py::arg("phSocket"))
 		.def("GetSocketInfo", [](ISteamNetworking* self, SNetSocket_t hSocket, uintptr_t pSteamIDRemote, uintptr_t peSocketStatus, uintptr_t punIPRemote, uintptr_t punPortRemote) { 
			CSteamID * ptr1 = reinterpret_cast<CSteamID *>(pSteamIDRemote);
			int * ptr2 = reinterpret_cast<int *>(peSocketStatus);
			SteamIPAddress_t * ptr3 = reinterpret_cast<SteamIPAddress_t *>(punIPRemote);
			uint16 * ptr4 = reinterpret_cast<uint16 *>(punPortRemote);
			return (bool)SteamAPI_ISteamNetworking_GetSocketInfo(self, hSocket, ptr1, ptr2, ptr3, ptr4); }, py::arg("hSocket"), py::arg("pSteamIDRemote"), py::arg("peSocketStatus"), py::arg("punIPRemote"), py::arg("punPortRemote"))
 		.def("GetListenSocketInfo", [](ISteamNetworking* self, SNetListenSocket_t hListenSocket, uintptr_t pnIP, uintptr_t pnPort) { 
			SteamIPAddress_t * ptr1 = reinterpret_cast<SteamIPAddress_t *>(pnIP);
			uint16 * ptr2 = reinterpret_cast<uint16 *>(pnPort);
			return (bool)SteamAPI_ISteamNetworking_GetListenSocketInfo(self, hListenSocket, ptr1, ptr2); }, py::arg("hListenSocket"), py::arg("pnIP"), py::arg("pnPort"))
 		.def("GetSocketConnectionType", &SteamAPI_ISteamNetworking_GetSocketConnectionType, py::arg("hSocket"))
 		.def("GetMaxPacketSize", &SteamAPI_ISteamNetworking_GetMaxPacketSize, py::arg("hSocket"))
 	;
 	m.def("SteamScreenshots",[]() -> ISteamScreenshots* { return SteamScreenshots(); }, py::return_value_policy::reference);
 	py::class_<ISteamScreenshots, std::unique_ptr<ISteamScreenshots, py::nodelete>> _isteamscreenshots(m, "ISteamScreenshots");
 	_isteamscreenshots
 		.def_property_readonly("ptr", [](ISteamScreenshots& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def("WriteScreenshot", [](ISteamScreenshots* self, uintptr_t pubRGB, uint32 cubRGB, int nWidth, int nHeight) { 
			void * ptr0 = reinterpret_cast<void *>(pubRGB);
			return (ScreenshotHandle)SteamAPI_ISteamScreenshots_WriteScreenshot(self, ptr0, cubRGB, nWidth, nHeight); }, py::arg("pubRGB"), py::arg("cubRGB"), py::arg("nWidth"), py::arg("nHeight"))
 		.def("AddScreenshotToLibrary", [](ISteamScreenshots* self, uintptr_t pchFilename, uintptr_t pchThumbnailFilename, int nWidth, int nHeight) { 
			const char * ptr0 = reinterpret_cast<const char *>(pchFilename);
			const char * ptr1 = reinterpret_cast<const char *>(pchThumbnailFilename);
			return (ScreenshotHandle)SteamAPI_ISteamScreenshots_AddScreenshotToLibrary(self, ptr0, ptr1, nWidth, nHeight); }, py::arg("pchFilename"), py::arg("pchThumbnailFilename"), py::arg("nWidth"), py::arg("nHeight"))
 		.def("TriggerScreenshot", &SteamAPI_ISteamScreenshots_TriggerScreenshot)
 		.def("HookScreenshots", &SteamAPI_ISteamScreenshots_HookScreenshots, py::arg("bHook"))
 		.def("SetLocation", [](ISteamScreenshots* self, ScreenshotHandle hScreenshot, uintptr_t pchLocation) { 
			const char * ptr1 = reinterpret_cast<const char *>(pchLocation);
			return (bool)SteamAPI_ISteamScreenshots_SetLocation(self, hScreenshot, ptr1); }, py::arg("hScreenshot"), py::arg("pchLocation"))
 		.def("TagUser", &SteamAPI_ISteamScreenshots_TagUser, py::arg("hScreenshot"), py::arg("steamID"))
 		.def("TagPublishedFile", &SteamAPI_ISteamScreenshots_TagPublishedFile, py::arg("hScreenshot"), py::arg("unPublishedFileID"))
 		.def("IsScreenshotsHooked", &SteamAPI_ISteamScreenshots_IsScreenshotsHooked)
 		.def("AddVRScreenshotToLibrary", [](ISteamScreenshots* self, EVRScreenshotType eType, uintptr_t pchFilename, uintptr_t pchVRFilename) { 
			const char * ptr1 = reinterpret_cast<const char *>(pchFilename);
			const char * ptr2 = reinterpret_cast<const char *>(pchVRFilename);
			return (ScreenshotHandle)SteamAPI_ISteamScreenshots_AddVRScreenshotToLibrary(self, eType, ptr1, ptr2); }, py::arg("eType"), py::arg("pchFilename"), py::arg("pchVRFilename"))
 	;
 	m.def("SteamMusic",[]() -> ISteamMusic* { return SteamMusic(); }, py::return_value_policy::reference);
 	py::class_<ISteamMusic, std::unique_ptr<ISteamMusic, py::nodelete>> _isteammusic(m, "ISteamMusic");
 	_isteammusic
 		.def_property_readonly("ptr", [](ISteamMusic& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def("BIsEnabled", &SteamAPI_ISteamMusic_BIsEnabled)
 		.def("BIsPlaying", &SteamAPI_ISteamMusic_BIsPlaying)
 		.def("GetPlaybackStatus", &SteamAPI_ISteamMusic_GetPlaybackStatus)
 		.def("Play", &SteamAPI_ISteamMusic_Play)
 		.def("Pause", &SteamAPI_ISteamMusic_Pause)
 		.def("PlayPrevious", &SteamAPI_ISteamMusic_PlayPrevious)
 		.def("PlayNext", &SteamAPI_ISteamMusic_PlayNext)
 		.def("SetVolume", &SteamAPI_ISteamMusic_SetVolume, py::arg("flVolume"))
 		.def("GetVolume", &SteamAPI_ISteamMusic_GetVolume)
 	;
 	m.def("SteamHTTP",[]() -> ISteamHTTP* { return SteamHTTP(); }, py::return_value_policy::reference);
 	py::class_<ISteamHTTP, std::unique_ptr<ISteamHTTP, py::nodelete>> _isteamhttp(m, "ISteamHTTP");
 	_isteamhttp
 		.def_property_readonly("ptr", [](ISteamHTTP& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def("CreateHTTPRequest", [](ISteamHTTP* self, EHTTPMethod eHTTPRequestMethod, uintptr_t pchAbsoluteURL) { 
			const char * ptr1 = reinterpret_cast<const char *>(pchAbsoluteURL);
			return (HTTPRequestHandle)SteamAPI_ISteamHTTP_CreateHTTPRequest(self, eHTTPRequestMethod, ptr1); }, py::arg("eHTTPRequestMethod"), py::arg("pchAbsoluteURL"))
 		.def("SetHTTPRequestContextValue", &SteamAPI_ISteamHTTP_SetHTTPRequestContextValue, py::arg("hRequest"), py::arg("ulContextValue"))
 		.def("SetHTTPRequestNetworkActivityTimeout", &SteamAPI_ISteamHTTP_SetHTTPRequestNetworkActivityTimeout, py::arg("hRequest"), py::arg("unTimeoutSeconds"))
 		.def("SetHTTPRequestHeaderValue", [](ISteamHTTP* self, HTTPRequestHandle hRequest, uintptr_t pchHeaderName, uintptr_t pchHeaderValue) { 
			const char * ptr1 = reinterpret_cast<const char *>(pchHeaderName);
			const char * ptr2 = reinterpret_cast<const char *>(pchHeaderValue);
			return (bool)SteamAPI_ISteamHTTP_SetHTTPRequestHeaderValue(self, hRequest, ptr1, ptr2); }, py::arg("hRequest"), py::arg("pchHeaderName"), py::arg("pchHeaderValue"))
 		.def("SetHTTPRequestGetOrPostParameter", [](ISteamHTTP* self, HTTPRequestHandle hRequest, uintptr_t pchParamName, uintptr_t pchParamValue) { 
			const char * ptr1 = reinterpret_cast<const char *>(pchParamName);
			const char * ptr2 = reinterpret_cast<const char *>(pchParamValue);
			return (bool)SteamAPI_ISteamHTTP_SetHTTPRequestGetOrPostParameter(self, hRequest, ptr1, ptr2); }, py::arg("hRequest"), py::arg("pchParamName"), py::arg("pchParamValue"))
 		.def("SendHTTPRequest", [](ISteamHTTP* self, HTTPRequestHandle hRequest, uintptr_t pCallHandle) { 
			SteamAPICall_t * ptr1 = reinterpret_cast<SteamAPICall_t *>(pCallHandle);
			return (bool)SteamAPI_ISteamHTTP_SendHTTPRequest(self, hRequest, ptr1); }, py::arg("hRequest"), py::arg("pCallHandle"))
 		.def("SendHTTPRequestAndStreamResponse", [](ISteamHTTP* self, HTTPRequestHandle hRequest, uintptr_t pCallHandle) { 
			SteamAPICall_t * ptr1 = reinterpret_cast<SteamAPICall_t *>(pCallHandle);
			return (bool)SteamAPI_ISteamHTTP_SendHTTPRequestAndStreamResponse(self, hRequest, ptr1); }, py::arg("hRequest"), py::arg("pCallHandle"))
 		.def("DeferHTTPRequest", &SteamAPI_ISteamHTTP_DeferHTTPRequest, py::arg("hRequest"))
 		.def("PrioritizeHTTPRequest", &SteamAPI_ISteamHTTP_PrioritizeHTTPRequest, py::arg("hRequest"))
 		.def("GetHTTPResponseHeaderSize", [](ISteamHTTP* self, HTTPRequestHandle hRequest, uintptr_t pchHeaderName, uintptr_t unResponseHeaderSize) { 
			const char * ptr1 = reinterpret_cast<const char *>(pchHeaderName);
			uint32 * ptr2 = reinterpret_cast<uint32 *>(unResponseHeaderSize);
			return (bool)SteamAPI_ISteamHTTP_GetHTTPResponseHeaderSize(self, hRequest, ptr1, ptr2); }, py::arg("hRequest"), py::arg("pchHeaderName"), py::arg("unResponseHeaderSize"))
 		.def("GetHTTPResponseHeaderValue", [](ISteamHTTP* self, HTTPRequestHandle hRequest, uintptr_t pchHeaderName, uintptr_t pHeaderValueBuffer, uint32 unBufferSize) { 
			const char * ptr1 = reinterpret_cast<const char *>(pchHeaderName);
			uint8 * ptr2 = reinterpret_cast<uint8 *>(pHeaderValueBuffer);
			return (bool)SteamAPI_ISteamHTTP_GetHTTPResponseHeaderValue(self, hRequest, ptr1, ptr2, unBufferSize); }, py::arg("hRequest"), py::arg("pchHeaderName"), py::arg("pHeaderValueBuffer"), py::arg("unBufferSize"))
 		.def("GetHTTPResponseBodySize", [](ISteamHTTP* self, HTTPRequestHandle hRequest, uintptr_t unBodySize) { 
			uint32 * ptr1 = reinterpret_cast<uint32 *>(unBodySize);
			return (bool)SteamAPI_ISteamHTTP_GetHTTPResponseBodySize(self, hRequest, ptr1); }, py::arg("hRequest"), py::arg("unBodySize"))
 		.def("GetHTTPResponseBodyData", [](ISteamHTTP* self, HTTPRequestHandle hRequest, uintptr_t pBodyDataBuffer, uint32 unBufferSize) { 
			uint8 * ptr1 = reinterpret_cast<uint8 *>(pBodyDataBuffer);
			return (bool)SteamAPI_ISteamHTTP_GetHTTPResponseBodyData(self, hRequest, ptr1, unBufferSize); }, py::arg("hRequest"), py::arg("pBodyDataBuffer"), py::arg("unBufferSize"))
 		.def("GetHTTPStreamingResponseBodyData", [](ISteamHTTP* self, HTTPRequestHandle hRequest, uint32 cOffset, uintptr_t pBodyDataBuffer, uint32 unBufferSize) { 
			uint8 * ptr2 = reinterpret_cast<uint8 *>(pBodyDataBuffer);
			return (bool)SteamAPI_ISteamHTTP_GetHTTPStreamingResponseBodyData(self, hRequest, cOffset, ptr2, unBufferSize); }, py::arg("hRequest"), py::arg("cOffset"), py::arg("pBodyDataBuffer"), py::arg("unBufferSize"))
 		.def("ReleaseHTTPRequest", &SteamAPI_ISteamHTTP_ReleaseHTTPRequest, py::arg("hRequest"))
 		.def("GetHTTPDownloadProgressPct", [](ISteamHTTP* self, HTTPRequestHandle hRequest, uintptr_t pflPercentOut) { 
			float * ptr1 = reinterpret_cast<float *>(pflPercentOut);
			return (bool)SteamAPI_ISteamHTTP_GetHTTPDownloadProgressPct(self, hRequest, ptr1); }, py::arg("hRequest"), py::arg("pflPercentOut"))
 		.def("SetHTTPRequestRawPostBody", [](ISteamHTTP* self, HTTPRequestHandle hRequest, uintptr_t pchContentType, uintptr_t pubBody, uint32 unBodyLen) { 
			const char * ptr1 = reinterpret_cast<const char *>(pchContentType);
			uint8 * ptr2 = reinterpret_cast<uint8 *>(pubBody);
			return (bool)SteamAPI_ISteamHTTP_SetHTTPRequestRawPostBody(self, hRequest, ptr1, ptr2, unBodyLen); }, py::arg("hRequest"), py::arg("pchContentType"), py::arg("pubBody"), py::arg("unBodyLen"))
 		.def("CreateCookieContainer", &SteamAPI_ISteamHTTP_CreateCookieContainer, py::arg("bAllowResponsesToModify"))
 		.def("ReleaseCookieContainer", &SteamAPI_ISteamHTTP_ReleaseCookieContainer, py::arg("hCookieContainer"))
 		.def("SetCookie", [](ISteamHTTP* self, HTTPCookieContainerHandle hCookieContainer, uintptr_t pchHost, uintptr_t pchUrl, uintptr_t pchCookie) { 
			const char * ptr1 = reinterpret_cast<const char *>(pchHost);
			const char * ptr2 = reinterpret_cast<const char *>(pchUrl);
			const char * ptr3 = reinterpret_cast<const char *>(pchCookie);
			return (bool)SteamAPI_ISteamHTTP_SetCookie(self, hCookieContainer, ptr1, ptr2, ptr3); }, py::arg("hCookieContainer"), py::arg("pchHost"), py::arg("pchUrl"), py::arg("pchCookie"))
 		.def("SetHTTPRequestCookieContainer", &SteamAPI_ISteamHTTP_SetHTTPRequestCookieContainer, py::arg("hRequest"), py::arg("hCookieContainer"))
 		.def("SetHTTPRequestUserAgentInfo", [](ISteamHTTP* self, HTTPRequestHandle hRequest, uintptr_t pchUserAgentInfo) { 
			const char * ptr1 = reinterpret_cast<const char *>(pchUserAgentInfo);
			return (bool)SteamAPI_ISteamHTTP_SetHTTPRequestUserAgentInfo(self, hRequest, ptr1); }, py::arg("hRequest"), py::arg("pchUserAgentInfo"))
 		.def("SetHTTPRequestRequiresVerifiedCertificate", &SteamAPI_ISteamHTTP_SetHTTPRequestRequiresVerifiedCertificate, py::arg("hRequest"), py::arg("bRequireVerifiedCertificate"))
 		.def("SetHTTPRequestAbsoluteTimeoutMS", &SteamAPI_ISteamHTTP_SetHTTPRequestAbsoluteTimeoutMS, py::arg("hRequest"), py::arg("unMilliseconds"))
 		.def("GetHTTPRequestWasTimedOut", [](ISteamHTTP* self, HTTPRequestHandle hRequest, uintptr_t pbWasTimedOut) { 
			bool * ptr1 = reinterpret_cast<bool *>(pbWasTimedOut);
			return (bool)SteamAPI_ISteamHTTP_GetHTTPRequestWasTimedOut(self, hRequest, ptr1); }, py::arg("hRequest"), py::arg("pbWasTimedOut"))
 	;
 	m.def("SteamInput",[]() -> ISteamInput* { return SteamInput(); }, py::return_value_policy::reference);
 	py::class_<ISteamInput, std::unique_ptr<ISteamInput, py::nodelete>> _isteaminput(m, "ISteamInput");
 	_isteaminput
 		.def_property_readonly("ptr", [](ISteamInput& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def("Init", &SteamAPI_ISteamInput_Init, py::arg("bExplicitlyCallRunFrame"))
 		.def("Shutdown", &SteamAPI_ISteamInput_Shutdown)
 		.def("SetInputActionManifestFilePath", [](ISteamInput* self, uintptr_t pchInputActionManifestAbsolutePath) { 
			const char * ptr0 = reinterpret_cast<const char *>(pchInputActionManifestAbsolutePath);
			return (bool)SteamAPI_ISteamInput_SetInputActionManifestFilePath(self, ptr0); }, py::arg("pchInputActionManifestAbsolutePath"))
 		.def("RunFrame", &SteamAPI_ISteamInput_RunFrame, py::arg("bReservedValue"))
 		.def("BWaitForData", &SteamAPI_ISteamInput_BWaitForData, py::arg("bWaitForever"), py::arg("unTimeout"))
 		.def("BNewDataAvailable", &SteamAPI_ISteamInput_BNewDataAvailable)
 		.def("GetConnectedControllers", [](ISteamInput* self, uintptr_t handlesOut) { 
			InputHandle_t * ptr0 = reinterpret_cast<InputHandle_t *>(handlesOut);
			return (int)SteamAPI_ISteamInput_GetConnectedControllers(self, ptr0); }, py::arg("handlesOut"))
 		.def("EnableDeviceCallbacks", &SteamAPI_ISteamInput_EnableDeviceCallbacks)
 		.def("EnableActionEventCallbacks", &SteamAPI_ISteamInput_EnableActionEventCallbacks, py::arg("pCallback"))
 		.def("GetActionSetHandle", [](ISteamInput* self, uintptr_t pszActionSetName) { 
			const char * ptr0 = reinterpret_cast<const char *>(pszActionSetName);
			return (InputActionSetHandle_t)SteamAPI_ISteamInput_GetActionSetHandle(self, ptr0); }, py::arg("pszActionSetName"))
 		.def("ActivateActionSet", &SteamAPI_ISteamInput_ActivateActionSet, py::arg("inputHandle"), py::arg("actionSetHandle"))
 		.def("GetCurrentActionSet", &SteamAPI_ISteamInput_GetCurrentActionSet, py::arg("inputHandle"))
 		.def("ActivateActionSetLayer", &SteamAPI_ISteamInput_ActivateActionSetLayer, py::arg("inputHandle"), py::arg("actionSetLayerHandle"))
 		.def("DeactivateActionSetLayer", &SteamAPI_ISteamInput_DeactivateActionSetLayer, py::arg("inputHandle"), py::arg("actionSetLayerHandle"))
 		.def("DeactivateAllActionSetLayers", &SteamAPI_ISteamInput_DeactivateAllActionSetLayers, py::arg("inputHandle"))
 		.def("GetActiveActionSetLayers", [](ISteamInput* self, InputHandle_t inputHandle, uintptr_t handlesOut) { 
			InputActionSetHandle_t * ptr1 = reinterpret_cast<InputActionSetHandle_t *>(handlesOut);
			return (int)SteamAPI_ISteamInput_GetActiveActionSetLayers(self, inputHandle, ptr1); }, py::arg("inputHandle"), py::arg("handlesOut"))
 		.def("GetDigitalActionHandle", [](ISteamInput* self, uintptr_t pszActionName) { 
			const char * ptr0 = reinterpret_cast<const char *>(pszActionName);
			return (InputDigitalActionHandle_t)SteamAPI_ISteamInput_GetDigitalActionHandle(self, ptr0); }, py::arg("pszActionName"))
 		.def("GetDigitalActionData", &SteamAPI_ISteamInput_GetDigitalActionData, py::arg("inputHandle"), py::arg("digitalActionHandle"))
 		.def("GetDigitalActionOrigins", [](ISteamInput* self, InputHandle_t inputHandle, InputActionSetHandle_t actionSetHandle, InputDigitalActionHandle_t digitalActionHandle, uintptr_t originsOut) { 
			EInputActionOrigin * ptr3 = reinterpret_cast<EInputActionOrigin *>(originsOut);
			return (int)SteamAPI_ISteamInput_GetDigitalActionOrigins(self, inputHandle, actionSetHandle, digitalActionHandle, ptr3); }, py::arg("inputHandle"), py::arg("actionSetHandle"), py::arg("digitalActionHandle"), py::arg("originsOut"))
 		.def("GetStringForDigitalActionName", &SteamAPI_ISteamInput_GetStringForDigitalActionName, py::arg("eActionHandle"))
 		.def("GetAnalogActionHandle", [](ISteamInput* self, uintptr_t pszActionName) { 
			const char * ptr0 = reinterpret_cast<const char *>(pszActionName);
			return (InputAnalogActionHandle_t)SteamAPI_ISteamInput_GetAnalogActionHandle(self, ptr0); }, py::arg("pszActionName"))
 		.def("GetAnalogActionData", &SteamAPI_ISteamInput_GetAnalogActionData, py::arg("inputHandle"), py::arg("analogActionHandle"))
 		.def("GetAnalogActionOrigins", [](ISteamInput* self, InputHandle_t inputHandle, InputActionSetHandle_t actionSetHandle, InputAnalogActionHandle_t analogActionHandle, uintptr_t originsOut) { 
			EInputActionOrigin * ptr3 = reinterpret_cast<EInputActionOrigin *>(originsOut);
			return (int)SteamAPI_ISteamInput_GetAnalogActionOrigins(self, inputHandle, actionSetHandle, analogActionHandle, ptr3); }, py::arg("inputHandle"), py::arg("actionSetHandle"), py::arg("analogActionHandle"), py::arg("originsOut"))
 		.def("GetGlyphPNGForActionOrigin", &SteamAPI_ISteamInput_GetGlyphPNGForActionOrigin, py::arg("eOrigin"), py::arg("eSize"), py::arg("unFlags"))
 		.def("GetGlyphSVGForActionOrigin", &SteamAPI_ISteamInput_GetGlyphSVGForActionOrigin, py::arg("eOrigin"), py::arg("unFlags"))
 		.def("GetGlyphForActionOrigin_Legacy", &SteamAPI_ISteamInput_GetGlyphForActionOrigin_Legacy, py::arg("eOrigin"))
 		.def("GetStringForActionOrigin", &SteamAPI_ISteamInput_GetStringForActionOrigin, py::arg("eOrigin"))
 		.def("GetStringForAnalogActionName", &SteamAPI_ISteamInput_GetStringForAnalogActionName, py::arg("eActionHandle"))
 		.def("StopAnalogActionMomentum", &SteamAPI_ISteamInput_StopAnalogActionMomentum, py::arg("inputHandle"), py::arg("eAction"))
 		.def("GetMotionData", &SteamAPI_ISteamInput_GetMotionData, py::arg("inputHandle"))
 		.def("TriggerVibration", &SteamAPI_ISteamInput_TriggerVibration, py::arg("inputHandle"), py::arg("usLeftSpeed"), py::arg("usRightSpeed"))
 		.def("TriggerVibrationExtended", &SteamAPI_ISteamInput_TriggerVibrationExtended, py::arg("inputHandle"), py::arg("usLeftSpeed"), py::arg("usRightSpeed"), py::arg("usLeftTriggerSpeed"), py::arg("usRightTriggerSpeed"))
 		.def("TriggerSimpleHapticEvent", &SteamAPI_ISteamInput_TriggerSimpleHapticEvent, py::arg("inputHandle"), py::arg("eHapticLocation"), py::arg("nIntensity"), py::arg("nGainDB"), py::arg("nOtherIntensity"), py::arg("nOtherGainDB"))
 		.def("SetLEDColor", &SteamAPI_ISteamInput_SetLEDColor, py::arg("inputHandle"), py::arg("nColorR"), py::arg("nColorG"), py::arg("nColorB"), py::arg("nFlags"))
 		.def("Legacy_TriggerHapticPulse", &SteamAPI_ISteamInput_Legacy_TriggerHapticPulse, py::arg("inputHandle"), py::arg("eTargetPad"), py::arg("usDurationMicroSec"))
 		.def("Legacy_TriggerRepeatedHapticPulse", &SteamAPI_ISteamInput_Legacy_TriggerRepeatedHapticPulse, py::arg("inputHandle"), py::arg("eTargetPad"), py::arg("usDurationMicroSec"), py::arg("usOffMicroSec"), py::arg("unRepeat"), py::arg("nFlags"))
 		.def("ShowBindingPanel", &SteamAPI_ISteamInput_ShowBindingPanel, py::arg("inputHandle"))
 		.def("GetInputTypeForHandle", &SteamAPI_ISteamInput_GetInputTypeForHandle, py::arg("inputHandle"))
 		.def("GetControllerForGamepadIndex", &SteamAPI_ISteamInput_GetControllerForGamepadIndex, py::arg("nIndex"))
 		.def("GetGamepadIndexForController", &SteamAPI_ISteamInput_GetGamepadIndexForController, py::arg("ulinputHandle"))
 		.def("GetStringForXboxOrigin", &SteamAPI_ISteamInput_GetStringForXboxOrigin, py::arg("eOrigin"))
 		.def("GetGlyphForXboxOrigin", &SteamAPI_ISteamInput_GetGlyphForXboxOrigin, py::arg("eOrigin"))
 		.def("GetActionOriginFromXboxOrigin", &SteamAPI_ISteamInput_GetActionOriginFromXboxOrigin, py::arg("inputHandle"), py::arg("eOrigin"))
 		.def("TranslateActionOrigin", &SteamAPI_ISteamInput_TranslateActionOrigin, py::arg("eDestinationInputType"), py::arg("eSourceOrigin"))
 		.def("GetDeviceBindingRevision", [](ISteamInput* self, InputHandle_t inputHandle, uintptr_t pMajor, uintptr_t pMinor) { 
			int * ptr1 = reinterpret_cast<int *>(pMajor);
			int * ptr2 = reinterpret_cast<int *>(pMinor);
			return (bool)SteamAPI_ISteamInput_GetDeviceBindingRevision(self, inputHandle, ptr1, ptr2); }, py::arg("inputHandle"), py::arg("pMajor"), py::arg("pMinor"))
 		.def("GetRemotePlaySessionID", &SteamAPI_ISteamInput_GetRemotePlaySessionID, py::arg("inputHandle"))
 		.def("GetSessionInputConfigurationSettings", &SteamAPI_ISteamInput_GetSessionInputConfigurationSettings)
 		.def("SetDualSenseTriggerEffect", [](ISteamInput* self, InputHandle_t inputHandle, uintptr_t pParam) { 
			const ScePadTriggerEffectParam * ptr1 = reinterpret_cast<const ScePadTriggerEffectParam *>(pParam);
			SteamAPI_ISteamInput_SetDualSenseTriggerEffect(self, inputHandle, ptr1); }, py::arg("inputHandle"), py::arg("pParam"))
 	;
 	m.def("SteamController",[]() -> ISteamController* { return SteamController(); }, py::return_value_policy::reference);
 	py::class_<ISteamController, std::unique_ptr<ISteamController, py::nodelete>> _isteamcontroller(m, "ISteamController");
 	_isteamcontroller
 		.def_property_readonly("ptr", [](ISteamController& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def("Init", &SteamAPI_ISteamController_Init)
 		.def("Shutdown", &SteamAPI_ISteamController_Shutdown)
 		.def("RunFrame", &SteamAPI_ISteamController_RunFrame)
 		.def("GetConnectedControllers", [](ISteamController* self, uintptr_t handlesOut) { 
			ControllerHandle_t * ptr0 = reinterpret_cast<ControllerHandle_t *>(handlesOut);
			return (int)SteamAPI_ISteamController_GetConnectedControllers(self, ptr0); }, py::arg("handlesOut"))
 		.def("GetActionSetHandle", [](ISteamController* self, uintptr_t pszActionSetName) { 
			const char * ptr0 = reinterpret_cast<const char *>(pszActionSetName);
			return (ControllerActionSetHandle_t)SteamAPI_ISteamController_GetActionSetHandle(self, ptr0); }, py::arg("pszActionSetName"))
 		.def("ActivateActionSet", &SteamAPI_ISteamController_ActivateActionSet, py::arg("controllerHandle"), py::arg("actionSetHandle"))
 		.def("GetCurrentActionSet", &SteamAPI_ISteamController_GetCurrentActionSet, py::arg("controllerHandle"))
 		.def("ActivateActionSetLayer", &SteamAPI_ISteamController_ActivateActionSetLayer, py::arg("controllerHandle"), py::arg("actionSetLayerHandle"))
 		.def("DeactivateActionSetLayer", &SteamAPI_ISteamController_DeactivateActionSetLayer, py::arg("controllerHandle"), py::arg("actionSetLayerHandle"))
 		.def("DeactivateAllActionSetLayers", &SteamAPI_ISteamController_DeactivateAllActionSetLayers, py::arg("controllerHandle"))
 		.def("GetActiveActionSetLayers", [](ISteamController* self, ControllerHandle_t controllerHandle, uintptr_t handlesOut) { 
			ControllerActionSetHandle_t * ptr1 = reinterpret_cast<ControllerActionSetHandle_t *>(handlesOut);
			return (int)SteamAPI_ISteamController_GetActiveActionSetLayers(self, controllerHandle, ptr1); }, py::arg("controllerHandle"), py::arg("handlesOut"))
 		.def("GetDigitalActionHandle", [](ISteamController* self, uintptr_t pszActionName) { 
			const char * ptr0 = reinterpret_cast<const char *>(pszActionName);
			return (ControllerDigitalActionHandle_t)SteamAPI_ISteamController_GetDigitalActionHandle(self, ptr0); }, py::arg("pszActionName"))
 		.def("GetDigitalActionData", &SteamAPI_ISteamController_GetDigitalActionData, py::arg("controllerHandle"), py::arg("digitalActionHandle"))
 		.def("GetDigitalActionOrigins", [](ISteamController* self, ControllerHandle_t controllerHandle, ControllerActionSetHandle_t actionSetHandle, ControllerDigitalActionHandle_t digitalActionHandle, uintptr_t originsOut) { 
			EControllerActionOrigin * ptr3 = reinterpret_cast<EControllerActionOrigin *>(originsOut);
			return (int)SteamAPI_ISteamController_GetDigitalActionOrigins(self, controllerHandle, actionSetHandle, digitalActionHandle, ptr3); }, py::arg("controllerHandle"), py::arg("actionSetHandle"), py::arg("digitalActionHandle"), py::arg("originsOut"))
 		.def("GetAnalogActionHandle", [](ISteamController* self, uintptr_t pszActionName) { 
			const char * ptr0 = reinterpret_cast<const char *>(pszActionName);
			return (ControllerAnalogActionHandle_t)SteamAPI_ISteamController_GetAnalogActionHandle(self, ptr0); }, py::arg("pszActionName"))
 		.def("GetAnalogActionData", &SteamAPI_ISteamController_GetAnalogActionData, py::arg("controllerHandle"), py::arg("analogActionHandle"))
 		.def("GetAnalogActionOrigins", [](ISteamController* self, ControllerHandle_t controllerHandle, ControllerActionSetHandle_t actionSetHandle, ControllerAnalogActionHandle_t analogActionHandle, uintptr_t originsOut) { 
			EControllerActionOrigin * ptr3 = reinterpret_cast<EControllerActionOrigin *>(originsOut);
			return (int)SteamAPI_ISteamController_GetAnalogActionOrigins(self, controllerHandle, actionSetHandle, analogActionHandle, ptr3); }, py::arg("controllerHandle"), py::arg("actionSetHandle"), py::arg("analogActionHandle"), py::arg("originsOut"))
 		.def("GetGlyphForActionOrigin", &SteamAPI_ISteamController_GetGlyphForActionOrigin, py::arg("eOrigin"))
 		.def("GetStringForActionOrigin", &SteamAPI_ISteamController_GetStringForActionOrigin, py::arg("eOrigin"))
 		.def("StopAnalogActionMomentum", &SteamAPI_ISteamController_StopAnalogActionMomentum, py::arg("controllerHandle"), py::arg("eAction"))
 		.def("GetMotionData", &SteamAPI_ISteamController_GetMotionData, py::arg("controllerHandle"))
 		.def("TriggerHapticPulse", &SteamAPI_ISteamController_TriggerHapticPulse, py::arg("controllerHandle"), py::arg("eTargetPad"), py::arg("usDurationMicroSec"))
 		.def("TriggerRepeatedHapticPulse", &SteamAPI_ISteamController_TriggerRepeatedHapticPulse, py::arg("controllerHandle"), py::arg("eTargetPad"), py::arg("usDurationMicroSec"), py::arg("usOffMicroSec"), py::arg("unRepeat"), py::arg("nFlags"))
 		.def("TriggerVibration", &SteamAPI_ISteamController_TriggerVibration, py::arg("controllerHandle"), py::arg("usLeftSpeed"), py::arg("usRightSpeed"))
 		.def("SetLEDColor", &SteamAPI_ISteamController_SetLEDColor, py::arg("controllerHandle"), py::arg("nColorR"), py::arg("nColorG"), py::arg("nColorB"), py::arg("nFlags"))
 		.def("ShowBindingPanel", &SteamAPI_ISteamController_ShowBindingPanel, py::arg("controllerHandle"))
 		.def("GetInputTypeForHandle", &SteamAPI_ISteamController_GetInputTypeForHandle, py::arg("controllerHandle"))
 		.def("GetControllerForGamepadIndex", &SteamAPI_ISteamController_GetControllerForGamepadIndex, py::arg("nIndex"))
 		.def("GetGamepadIndexForController", &SteamAPI_ISteamController_GetGamepadIndexForController, py::arg("ulControllerHandle"))
 		.def("GetStringForXboxOrigin", &SteamAPI_ISteamController_GetStringForXboxOrigin, py::arg("eOrigin"))
 		.def("GetGlyphForXboxOrigin", &SteamAPI_ISteamController_GetGlyphForXboxOrigin, py::arg("eOrigin"))
 		.def("GetActionOriginFromXboxOrigin", &SteamAPI_ISteamController_GetActionOriginFromXboxOrigin, py::arg("controllerHandle"), py::arg("eOrigin"))
 		.def("TranslateActionOrigin", &SteamAPI_ISteamController_TranslateActionOrigin, py::arg("eDestinationInputType"), py::arg("eSourceOrigin"))
 		.def("GetControllerBindingRevision", [](ISteamController* self, ControllerHandle_t controllerHandle, uintptr_t pMajor, uintptr_t pMinor) { 
			int * ptr1 = reinterpret_cast<int *>(pMajor);
			int * ptr2 = reinterpret_cast<int *>(pMinor);
			return (bool)SteamAPI_ISteamController_GetControllerBindingRevision(self, controllerHandle, ptr1, ptr2); }, py::arg("controllerHandle"), py::arg("pMajor"), py::arg("pMinor"))
 	;
 	m.def("SteamUGC",[]() -> ISteamUGC* { return SteamUGC(); }, py::return_value_policy::reference);
 	py::class_<ISteamUGC, std::unique_ptr<ISteamUGC, py::nodelete>> _isteamugc(m, "ISteamUGC");
 	_isteamugc
 		.def_property_readonly("ptr", [](ISteamUGC& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def("CreateQueryUserUGCRequest", &SteamAPI_ISteamUGC_CreateQueryUserUGCRequest, py::arg("unAccountID"), py::arg("eListType"), py::arg("eMatchingUGCType"), py::arg("eSortOrder"), py::arg("nCreatorAppID"), py::arg("nConsumerAppID"), py::arg("unPage"))
 		.def("CreateQueryAllUGCRequest", [](ISteamUGC* self, EUGCQuery eQueryType, EUGCMatchingUGCType eMatchingeMatchingUGCTypeFileType, AppId_t nCreatorAppID, AppId_t nConsumerAppID, uintptr_t pchCursor) { 
			const char * ptr4 = reinterpret_cast<const char *>(pchCursor);
			return (UGCQueryHandle_t)SteamAPI_ISteamUGC_CreateQueryAllUGCRequestCursor(self, eQueryType, eMatchingeMatchingUGCTypeFileType, nCreatorAppID, nConsumerAppID, ptr4); }, py::arg("eQueryType"), py::arg("eMatchingeMatchingUGCTypeFileType"), py::arg("nCreatorAppID"), py::arg("nConsumerAppID"), py::arg("pchCursor"))
 		.def("CreateQueryUGCDetailsRequest", [](ISteamUGC* self, uintptr_t pvecPublishedFileID, uint32 unNumPublishedFileIDs) { 
			PublishedFileId_t * ptr0 = reinterpret_cast<PublishedFileId_t *>(pvecPublishedFileID);
			return (UGCQueryHandle_t)SteamAPI_ISteamUGC_CreateQueryUGCDetailsRequest(self, ptr0, unNumPublishedFileIDs); }, py::arg("pvecPublishedFileID"), py::arg("unNumPublishedFileIDs"))
 		.def("SendQueryUGCRequest", &SteamAPI_ISteamUGC_SendQueryUGCRequest, py::arg("handle"))
 		.def("GetQueryUGCResult", [](ISteamUGC* self, UGCQueryHandle_t handle, uint32 index, uintptr_t pDetails) { 
			SteamUGCDetails_t * ptr2 = reinterpret_cast<SteamUGCDetails_t *>(pDetails);
			return (bool)SteamAPI_ISteamUGC_GetQueryUGCResult(self, handle, index, ptr2); }, py::arg("handle"), py::arg("index"), py::arg("pDetails"))
 		.def("GetQueryUGCNumTags", &SteamAPI_ISteamUGC_GetQueryUGCNumTags, py::arg("handle"), py::arg("index"))
 		.def("GetQueryUGCTag", [](ISteamUGC* self, UGCQueryHandle_t handle, uint32 index, uint32 indexTag, uintptr_t pchValue, uint32 cchValueSize) { 
			char * ptr3 = reinterpret_cast<char *>(pchValue);
			return (bool)SteamAPI_ISteamUGC_GetQueryUGCTag(self, handle, index, indexTag, ptr3, cchValueSize); }, py::arg("handle"), py::arg("index"), py::arg("indexTag"), py::arg("pchValue"), py::arg("cchValueSize"))
 		.def("GetQueryUGCTagDisplayName", [](ISteamUGC* self, UGCQueryHandle_t handle, uint32 index, uint32 indexTag, uintptr_t pchValue, uint32 cchValueSize) { 
			char * ptr3 = reinterpret_cast<char *>(pchValue);
			return (bool)SteamAPI_ISteamUGC_GetQueryUGCTagDisplayName(self, handle, index, indexTag, ptr3, cchValueSize); }, py::arg("handle"), py::arg("index"), py::arg("indexTag"), py::arg("pchValue"), py::arg("cchValueSize"))
 		.def("GetQueryUGCPreviewURL", [](ISteamUGC* self, UGCQueryHandle_t handle, uint32 index, uintptr_t pchURL, uint32 cchURLSize) { 
			char * ptr2 = reinterpret_cast<char *>(pchURL);
			return (bool)SteamAPI_ISteamUGC_GetQueryUGCPreviewURL(self, handle, index, ptr2, cchURLSize); }, py::arg("handle"), py::arg("index"), py::arg("pchURL"), py::arg("cchURLSize"))
 		.def("GetQueryUGCMetadata", [](ISteamUGC* self, UGCQueryHandle_t handle, uint32 index, uintptr_t pchMetadata, uint32 cchMetadatasize) { 
			char * ptr2 = reinterpret_cast<char *>(pchMetadata);
			return (bool)SteamAPI_ISteamUGC_GetQueryUGCMetadata(self, handle, index, ptr2, cchMetadatasize); }, py::arg("handle"), py::arg("index"), py::arg("pchMetadata"), py::arg("cchMetadatasize"))
 		.def("GetQueryUGCChildren", [](ISteamUGC* self, UGCQueryHandle_t handle, uint32 index, uintptr_t pvecPublishedFileID, uint32 cMaxEntries) { 
			PublishedFileId_t * ptr2 = reinterpret_cast<PublishedFileId_t *>(pvecPublishedFileID);
			return (bool)SteamAPI_ISteamUGC_GetQueryUGCChildren(self, handle, index, ptr2, cMaxEntries); }, py::arg("handle"), py::arg("index"), py::arg("pvecPublishedFileID"), py::arg("cMaxEntries"))
 		.def("GetQueryUGCStatistic", [](ISteamUGC* self, UGCQueryHandle_t handle, uint32 index, EItemStatistic eStatType, uintptr_t pStatValue) { 
			uint64 * ptr3 = reinterpret_cast<uint64 *>(pStatValue);
			return (bool)SteamAPI_ISteamUGC_GetQueryUGCStatistic(self, handle, index, eStatType, ptr3); }, py::arg("handle"), py::arg("index"), py::arg("eStatType"), py::arg("pStatValue"))
 		.def("GetQueryUGCNumAdditionalPreviews", &SteamAPI_ISteamUGC_GetQueryUGCNumAdditionalPreviews, py::arg("handle"), py::arg("index"))
 		.def("GetQueryUGCAdditionalPreview", [](ISteamUGC* self, UGCQueryHandle_t handle, uint32 index, uint32 previewIndex, uintptr_t pchURLOrVideoID, uint32 cchURLSize, uintptr_t pchOriginalFileName, uint32 cchOriginalFileNameSize, uintptr_t pPreviewType) { 
			char * ptr3 = reinterpret_cast<char *>(pchURLOrVideoID);
			char * ptr5 = reinterpret_cast<char *>(pchOriginalFileName);
			EItemPreviewType * ptr7 = reinterpret_cast<EItemPreviewType *>(pPreviewType);
			return (bool)SteamAPI_ISteamUGC_GetQueryUGCAdditionalPreview(self, handle, index, previewIndex, ptr3, cchURLSize, ptr5, cchOriginalFileNameSize, ptr7); }, py::arg("handle"), py::arg("index"), py::arg("previewIndex"), py::arg("pchURLOrVideoID"), py::arg("cchURLSize"), py::arg("pchOriginalFileName"), py::arg("cchOriginalFileNameSize"), py::arg("pPreviewType"))
 		.def("GetQueryUGCNumKeyValueTags", &SteamAPI_ISteamUGC_GetQueryUGCNumKeyValueTags, py::arg("handle"), py::arg("index"))
 		.def("GetQueryUGCKeyValueTag", [](ISteamUGC* self, UGCQueryHandle_t handle, uint32 index, uintptr_t pchKey, uintptr_t pchValue, uint32 cchValueSize) { 
			const char * ptr2 = reinterpret_cast<const char *>(pchKey);
			char * ptr3 = reinterpret_cast<char *>(pchValue);
			return (bool)SteamAPI_ISteamUGC_GetQueryFirstUGCKeyValueTag(self, handle, index, ptr2, ptr3, cchValueSize); }, py::arg("handle"), py::arg("index"), py::arg("pchKey"), py::arg("pchValue"), py::arg("cchValueSize"))
 		.def("GetNumSupportedGameVersions", &SteamAPI_ISteamUGC_GetNumSupportedGameVersions, py::arg("handle"), py::arg("index"))
 		.def("GetSupportedGameVersionData", [](ISteamUGC* self, UGCQueryHandle_t handle, uint32 index, uint32 versionIndex, uintptr_t pchGameBranchMin, uintptr_t pchGameBranchMax, uint32 cchGameBranchSize) { 
			char * ptr3 = reinterpret_cast<char *>(pchGameBranchMin);
			char * ptr4 = reinterpret_cast<char *>(pchGameBranchMax);
			return (bool)SteamAPI_ISteamUGC_GetSupportedGameVersionData(self, handle, index, versionIndex, ptr3, ptr4, cchGameBranchSize); }, py::arg("handle"), py::arg("index"), py::arg("versionIndex"), py::arg("pchGameBranchMin"), py::arg("pchGameBranchMax"), py::arg("cchGameBranchSize"))
 		.def("GetQueryUGCContentDescriptors", [](ISteamUGC* self, UGCQueryHandle_t handle, uint32 index, uintptr_t pvecDescriptors, uint32 cMaxEntries) { 
			EUGCContentDescriptorID * ptr2 = reinterpret_cast<EUGCContentDescriptorID *>(pvecDescriptors);
			return (uint32)SteamAPI_ISteamUGC_GetQueryUGCContentDescriptors(self, handle, index, ptr2, cMaxEntries); }, py::arg("handle"), py::arg("index"), py::arg("pvecDescriptors"), py::arg("cMaxEntries"))
 		.def("ReleaseQueryUGCRequest", &SteamAPI_ISteamUGC_ReleaseQueryUGCRequest, py::arg("handle"))
 		.def("AddRequiredTag", [](ISteamUGC* self, UGCQueryHandle_t handle, uintptr_t pTagName) { 
			const char * ptr1 = reinterpret_cast<const char *>(pTagName);
			return (bool)SteamAPI_ISteamUGC_AddRequiredTag(self, handle, ptr1); }, py::arg("handle"), py::arg("pTagName"))
 		.def("AddRequiredTagGroup", [](ISteamUGC* self, UGCQueryHandle_t handle, uintptr_t pTagGroups) { 
			const SteamParamStringArray_t * ptr1 = reinterpret_cast<const SteamParamStringArray_t *>(pTagGroups);
			return (bool)SteamAPI_ISteamUGC_AddRequiredTagGroup(self, handle, ptr1); }, py::arg("handle"), py::arg("pTagGroups"))
 		.def("AddExcludedTag", [](ISteamUGC* self, UGCQueryHandle_t handle, uintptr_t pTagName) { 
			const char * ptr1 = reinterpret_cast<const char *>(pTagName);
			return (bool)SteamAPI_ISteamUGC_AddExcludedTag(self, handle, ptr1); }, py::arg("handle"), py::arg("pTagName"))
 		.def("SetReturnOnlyIDs", &SteamAPI_ISteamUGC_SetReturnOnlyIDs, py::arg("handle"), py::arg("bReturnOnlyIDs"))
 		.def("SetReturnKeyValueTags", &SteamAPI_ISteamUGC_SetReturnKeyValueTags, py::arg("handle"), py::arg("bReturnKeyValueTags"))
 		.def("SetReturnLongDescription", &SteamAPI_ISteamUGC_SetReturnLongDescription, py::arg("handle"), py::arg("bReturnLongDescription"))
 		.def("SetReturnMetadata", &SteamAPI_ISteamUGC_SetReturnMetadata, py::arg("handle"), py::arg("bReturnMetadata"))
 		.def("SetReturnChildren", &SteamAPI_ISteamUGC_SetReturnChildren, py::arg("handle"), py::arg("bReturnChildren"))
 		.def("SetReturnAdditionalPreviews", &SteamAPI_ISteamUGC_SetReturnAdditionalPreviews, py::arg("handle"), py::arg("bReturnAdditionalPreviews"))
 		.def("SetReturnTotalOnly", &SteamAPI_ISteamUGC_SetReturnTotalOnly, py::arg("handle"), py::arg("bReturnTotalOnly"))
 		.def("SetReturnPlaytimeStats", &SteamAPI_ISteamUGC_SetReturnPlaytimeStats, py::arg("handle"), py::arg("unDays"))
 		.def("SetLanguage", [](ISteamUGC* self, UGCQueryHandle_t handle, uintptr_t pchLanguage) { 
			const char * ptr1 = reinterpret_cast<const char *>(pchLanguage);
			return (bool)SteamAPI_ISteamUGC_SetLanguage(self, handle, ptr1); }, py::arg("handle"), py::arg("pchLanguage"))
 		.def("SetAllowCachedResponse", &SteamAPI_ISteamUGC_SetAllowCachedResponse, py::arg("handle"), py::arg("unMaxAgeSeconds"))
 		.def("SetAdminQuery", &SteamAPI_ISteamUGC_SetAdminQuery, py::arg("handle"), py::arg("bAdminQuery"))
 		.def("SetCloudFileNameFilter", [](ISteamUGC* self, UGCQueryHandle_t handle, uintptr_t pMatchCloudFileName) { 
			const char * ptr1 = reinterpret_cast<const char *>(pMatchCloudFileName);
			return (bool)SteamAPI_ISteamUGC_SetCloudFileNameFilter(self, handle, ptr1); }, py::arg("handle"), py::arg("pMatchCloudFileName"))
 		.def("SetMatchAnyTag", &SteamAPI_ISteamUGC_SetMatchAnyTag, py::arg("handle"), py::arg("bMatchAnyTag"))
 		.def("SetSearchText", [](ISteamUGC* self, UGCQueryHandle_t handle, uintptr_t pSearchText) { 
			const char * ptr1 = reinterpret_cast<const char *>(pSearchText);
			return (bool)SteamAPI_ISteamUGC_SetSearchText(self, handle, ptr1); }, py::arg("handle"), py::arg("pSearchText"))
 		.def("SetRankedByTrendDays", &SteamAPI_ISteamUGC_SetRankedByTrendDays, py::arg("handle"), py::arg("unDays"))
 		.def("SetTimeCreatedDateRange", &SteamAPI_ISteamUGC_SetTimeCreatedDateRange, py::arg("handle"), py::arg("rtStart"), py::arg("rtEnd"))
 		.def("SetTimeUpdatedDateRange", &SteamAPI_ISteamUGC_SetTimeUpdatedDateRange, py::arg("handle"), py::arg("rtStart"), py::arg("rtEnd"))
 		.def("AddRequiredKeyValueTag", [](ISteamUGC* self, UGCQueryHandle_t handle, uintptr_t pKey, uintptr_t pValue) { 
			const char * ptr1 = reinterpret_cast<const char *>(pKey);
			const char * ptr2 = reinterpret_cast<const char *>(pValue);
			return (bool)SteamAPI_ISteamUGC_AddRequiredKeyValueTag(self, handle, ptr1, ptr2); }, py::arg("handle"), py::arg("pKey"), py::arg("pValue"))
 		.def("RequestUGCDetails", &SteamAPI_ISteamUGC_RequestUGCDetails, py::arg("nPublishedFileID"), py::arg("unMaxAgeSeconds"))
 		.def("CreateItem", &SteamAPI_ISteamUGC_CreateItem, py::arg("nConsumerAppId"), py::arg("eFileType"))
 		.def("StartItemUpdate", &SteamAPI_ISteamUGC_StartItemUpdate, py::arg("nConsumerAppId"), py::arg("nPublishedFileID"))
 		.def("SetItemTitle", [](ISteamUGC* self, UGCUpdateHandle_t handle, uintptr_t pchTitle) { 
			const char * ptr1 = reinterpret_cast<const char *>(pchTitle);
			return (bool)SteamAPI_ISteamUGC_SetItemTitle(self, handle, ptr1); }, py::arg("handle"), py::arg("pchTitle"))
 		.def("SetItemDescription", [](ISteamUGC* self, UGCUpdateHandle_t handle, uintptr_t pchDescription) { 
			const char * ptr1 = reinterpret_cast<const char *>(pchDescription);
			return (bool)SteamAPI_ISteamUGC_SetItemDescription(self, handle, ptr1); }, py::arg("handle"), py::arg("pchDescription"))
 		.def("SetItemUpdateLanguage", [](ISteamUGC* self, UGCUpdateHandle_t handle, uintptr_t pchLanguage) { 
			const char * ptr1 = reinterpret_cast<const char *>(pchLanguage);
			return (bool)SteamAPI_ISteamUGC_SetItemUpdateLanguage(self, handle, ptr1); }, py::arg("handle"), py::arg("pchLanguage"))
 		.def("SetItemMetadata", [](ISteamUGC* self, UGCUpdateHandle_t handle, uintptr_t pchMetaData) { 
			const char * ptr1 = reinterpret_cast<const char *>(pchMetaData);
			return (bool)SteamAPI_ISteamUGC_SetItemMetadata(self, handle, ptr1); }, py::arg("handle"), py::arg("pchMetaData"))
 		.def("SetItemVisibility", &SteamAPI_ISteamUGC_SetItemVisibility, py::arg("handle"), py::arg("eVisibility"))
 		.def("SetItemTags", [](ISteamUGC* self, UGCUpdateHandle_t updateHandle, uintptr_t pTags, bool bAllowAdminTags) { 
			const SteamParamStringArray_t * ptr1 = reinterpret_cast<const SteamParamStringArray_t *>(pTags);
			return (bool)SteamAPI_ISteamUGC_SetItemTags(self, updateHandle, ptr1, bAllowAdminTags); }, py::arg("updateHandle"), py::arg("pTags"), py::arg("bAllowAdminTags"))
 		.def("SetItemContent", [](ISteamUGC* self, UGCUpdateHandle_t handle, uintptr_t pszContentFolder) { 
			const char * ptr1 = reinterpret_cast<const char *>(pszContentFolder);
			return (bool)SteamAPI_ISteamUGC_SetItemContent(self, handle, ptr1); }, py::arg("handle"), py::arg("pszContentFolder"))
 		.def("SetItemPreview", [](ISteamUGC* self, UGCUpdateHandle_t handle, uintptr_t pszPreviewFile) { 
			const char * ptr1 = reinterpret_cast<const char *>(pszPreviewFile);
			return (bool)SteamAPI_ISteamUGC_SetItemPreview(self, handle, ptr1); }, py::arg("handle"), py::arg("pszPreviewFile"))
 		.def("SetAllowLegacyUpload", &SteamAPI_ISteamUGC_SetAllowLegacyUpload, py::arg("handle"), py::arg("bAllowLegacyUpload"))
 		.def("RemoveAllItemKeyValueTags", &SteamAPI_ISteamUGC_RemoveAllItemKeyValueTags, py::arg("handle"))
 		.def("RemoveItemKeyValueTags", [](ISteamUGC* self, UGCUpdateHandle_t handle, uintptr_t pchKey) { 
			const char * ptr1 = reinterpret_cast<const char *>(pchKey);
			return (bool)SteamAPI_ISteamUGC_RemoveItemKeyValueTags(self, handle, ptr1); }, py::arg("handle"), py::arg("pchKey"))
 		.def("AddItemKeyValueTag", [](ISteamUGC* self, UGCUpdateHandle_t handle, uintptr_t pchKey, uintptr_t pchValue) { 
			const char * ptr1 = reinterpret_cast<const char *>(pchKey);
			const char * ptr2 = reinterpret_cast<const char *>(pchValue);
			return (bool)SteamAPI_ISteamUGC_AddItemKeyValueTag(self, handle, ptr1, ptr2); }, py::arg("handle"), py::arg("pchKey"), py::arg("pchValue"))
 		.def("AddItemPreviewFile", [](ISteamUGC* self, UGCUpdateHandle_t handle, uintptr_t pszPreviewFile, EItemPreviewType type) { 
			const char * ptr1 = reinterpret_cast<const char *>(pszPreviewFile);
			return (bool)SteamAPI_ISteamUGC_AddItemPreviewFile(self, handle, ptr1, type); }, py::arg("handle"), py::arg("pszPreviewFile"), py::arg("type"))
 		.def("AddItemPreviewVideo", [](ISteamUGC* self, UGCUpdateHandle_t handle, uintptr_t pszVideoID) { 
			const char * ptr1 = reinterpret_cast<const char *>(pszVideoID);
			return (bool)SteamAPI_ISteamUGC_AddItemPreviewVideo(self, handle, ptr1); }, py::arg("handle"), py::arg("pszVideoID"))
 		.def("UpdateItemPreviewFile", [](ISteamUGC* self, UGCUpdateHandle_t handle, uint32 index, uintptr_t pszPreviewFile) { 
			const char * ptr2 = reinterpret_cast<const char *>(pszPreviewFile);
			return (bool)SteamAPI_ISteamUGC_UpdateItemPreviewFile(self, handle, index, ptr2); }, py::arg("handle"), py::arg("index"), py::arg("pszPreviewFile"))
 		.def("UpdateItemPreviewVideo", [](ISteamUGC* self, UGCUpdateHandle_t handle, uint32 index, uintptr_t pszVideoID) { 
			const char * ptr2 = reinterpret_cast<const char *>(pszVideoID);
			return (bool)SteamAPI_ISteamUGC_UpdateItemPreviewVideo(self, handle, index, ptr2); }, py::arg("handle"), py::arg("index"), py::arg("pszVideoID"))
 		.def("RemoveItemPreview", &SteamAPI_ISteamUGC_RemoveItemPreview, py::arg("handle"), py::arg("index"))
 		.def("AddContentDescriptor", &SteamAPI_ISteamUGC_AddContentDescriptor, py::arg("handle"), py::arg("descid"))
 		.def("RemoveContentDescriptor", &SteamAPI_ISteamUGC_RemoveContentDescriptor, py::arg("handle"), py::arg("descid"))
 		.def("SetRequiredGameVersions", [](ISteamUGC* self, UGCUpdateHandle_t handle, uintptr_t pszGameBranchMin, uintptr_t pszGameBranchMax) { 
			const char * ptr1 = reinterpret_cast<const char *>(pszGameBranchMin);
			const char * ptr2 = reinterpret_cast<const char *>(pszGameBranchMax);
			return (bool)SteamAPI_ISteamUGC_SetRequiredGameVersions(self, handle, ptr1, ptr2); }, py::arg("handle"), py::arg("pszGameBranchMin"), py::arg("pszGameBranchMax"))
 		.def("SubmitItemUpdate", [](ISteamUGC* self, UGCUpdateHandle_t handle, uintptr_t pchChangeNote) { 
			const char * ptr1 = reinterpret_cast<const char *>(pchChangeNote);
			return (SteamAPICall_t)SteamAPI_ISteamUGC_SubmitItemUpdate(self, handle, ptr1); }, py::arg("handle"), py::arg("pchChangeNote"))
 		.def("GetItemUpdateProgress", [](ISteamUGC* self, UGCUpdateHandle_t handle, uintptr_t punBytesProcessed, uintptr_t punBytesTotal) { 
			uint64 * ptr1 = reinterpret_cast<uint64 *>(punBytesProcessed);
			uint64 * ptr2 = reinterpret_cast<uint64 *>(punBytesTotal);
			return (EItemUpdateStatus)SteamAPI_ISteamUGC_GetItemUpdateProgress(self, handle, ptr1, ptr2); }, py::arg("handle"), py::arg("punBytesProcessed"), py::arg("punBytesTotal"))
 		.def("SetUserItemVote", &SteamAPI_ISteamUGC_SetUserItemVote, py::arg("nPublishedFileID"), py::arg("bVoteUp"))
 		.def("GetUserItemVote", &SteamAPI_ISteamUGC_GetUserItemVote, py::arg("nPublishedFileID"))
 		.def("AddItemToFavorites", &SteamAPI_ISteamUGC_AddItemToFavorites, py::arg("nAppId"), py::arg("nPublishedFileID"))
 		.def("RemoveItemFromFavorites", &SteamAPI_ISteamUGC_RemoveItemFromFavorites, py::arg("nAppId"), py::arg("nPublishedFileID"))
 		.def("SubscribeItem", &SteamAPI_ISteamUGC_SubscribeItem, py::arg("nPublishedFileID"))
 		.def("UnsubscribeItem", &SteamAPI_ISteamUGC_UnsubscribeItem, py::arg("nPublishedFileID"))
 		.def("GetNumSubscribedItems", &SteamAPI_ISteamUGC_GetNumSubscribedItems, py::arg("bIncludeLocallyDisabled"))
 		.def("GetSubscribedItems", [](ISteamUGC* self, uintptr_t pvecPublishedFileID, uint32 cMaxEntries, bool bIncludeLocallyDisabled) { 
			PublishedFileId_t * ptr0 = reinterpret_cast<PublishedFileId_t *>(pvecPublishedFileID);
			return (uint32)SteamAPI_ISteamUGC_GetSubscribedItems(self, ptr0, cMaxEntries, bIncludeLocallyDisabled); }, py::arg("pvecPublishedFileID"), py::arg("cMaxEntries"), py::arg("bIncludeLocallyDisabled"))
 		.def("GetItemState", &SteamAPI_ISteamUGC_GetItemState, py::arg("nPublishedFileID"))
 		.def("GetItemInstallInfo", [](ISteamUGC* self, PublishedFileId_t nPublishedFileID, uintptr_t punSizeOnDisk, uintptr_t pchFolder, uint32 cchFolderSize, uintptr_t punTimeStamp) { 
			uint64 * ptr1 = reinterpret_cast<uint64 *>(punSizeOnDisk);
			char * ptr2 = reinterpret_cast<char *>(pchFolder);
			uint32 * ptr4 = reinterpret_cast<uint32 *>(punTimeStamp);
			return (bool)SteamAPI_ISteamUGC_GetItemInstallInfo(self, nPublishedFileID, ptr1, ptr2, cchFolderSize, ptr4); }, py::arg("nPublishedFileID"), py::arg("punSizeOnDisk"), py::arg("pchFolder"), py::arg("cchFolderSize"), py::arg("punTimeStamp"))
 		.def("GetItemDownloadInfo", [](ISteamUGC* self, PublishedFileId_t nPublishedFileID, uintptr_t punBytesDownloaded, uintptr_t punBytesTotal) { 
			uint64 * ptr1 = reinterpret_cast<uint64 *>(punBytesDownloaded);
			uint64 * ptr2 = reinterpret_cast<uint64 *>(punBytesTotal);
			return (bool)SteamAPI_ISteamUGC_GetItemDownloadInfo(self, nPublishedFileID, ptr1, ptr2); }, py::arg("nPublishedFileID"), py::arg("punBytesDownloaded"), py::arg("punBytesTotal"))
 		.def("DownloadItem", &SteamAPI_ISteamUGC_DownloadItem, py::arg("nPublishedFileID"), py::arg("bHighPriority"))
 		.def("BInitWorkshopForGameServer", [](ISteamUGC* self, DepotId_t unWorkshopDepotID, uintptr_t pszFolder) { 
			const char * ptr1 = reinterpret_cast<const char *>(pszFolder);
			return (bool)SteamAPI_ISteamUGC_BInitWorkshopForGameServer(self, unWorkshopDepotID, ptr1); }, py::arg("unWorkshopDepotID"), py::arg("pszFolder"))
 		.def("SuspendDownloads", &SteamAPI_ISteamUGC_SuspendDownloads, py::arg("bSuspend"))
 		.def("StartPlaytimeTracking", [](ISteamUGC* self, uintptr_t pvecPublishedFileID, uint32 unNumPublishedFileIDs) { 
			PublishedFileId_t * ptr0 = reinterpret_cast<PublishedFileId_t *>(pvecPublishedFileID);
			return (SteamAPICall_t)SteamAPI_ISteamUGC_StartPlaytimeTracking(self, ptr0, unNumPublishedFileIDs); }, py::arg("pvecPublishedFileID"), py::arg("unNumPublishedFileIDs"))
 		.def("StopPlaytimeTracking", [](ISteamUGC* self, uintptr_t pvecPublishedFileID, uint32 unNumPublishedFileIDs) { 
			PublishedFileId_t * ptr0 = reinterpret_cast<PublishedFileId_t *>(pvecPublishedFileID);
			return (SteamAPICall_t)SteamAPI_ISteamUGC_StopPlaytimeTracking(self, ptr0, unNumPublishedFileIDs); }, py::arg("pvecPublishedFileID"), py::arg("unNumPublishedFileIDs"))
 		.def("StopPlaytimeTrackingForAllItems", &SteamAPI_ISteamUGC_StopPlaytimeTrackingForAllItems)
 		.def("AddDependency", &SteamAPI_ISteamUGC_AddDependency, py::arg("nParentPublishedFileID"), py::arg("nChildPublishedFileID"))
 		.def("RemoveDependency", &SteamAPI_ISteamUGC_RemoveDependency, py::arg("nParentPublishedFileID"), py::arg("nChildPublishedFileID"))
 		.def("AddAppDependency", &SteamAPI_ISteamUGC_AddAppDependency, py::arg("nPublishedFileID"), py::arg("nAppID"))
 		.def("RemoveAppDependency", &SteamAPI_ISteamUGC_RemoveAppDependency, py::arg("nPublishedFileID"), py::arg("nAppID"))
 		.def("GetAppDependencies", &SteamAPI_ISteamUGC_GetAppDependencies, py::arg("nPublishedFileID"))
 		.def("DeleteItem", &SteamAPI_ISteamUGC_DeleteItem, py::arg("nPublishedFileID"))
 		.def("ShowWorkshopEULA", &SteamAPI_ISteamUGC_ShowWorkshopEULA)
 		.def("GetWorkshopEULAStatus", &SteamAPI_ISteamUGC_GetWorkshopEULAStatus)
 		.def("GetUserContentDescriptorPreferences", [](ISteamUGC* self, uintptr_t pvecDescriptors, uint32 cMaxEntries) { 
			EUGCContentDescriptorID * ptr0 = reinterpret_cast<EUGCContentDescriptorID *>(pvecDescriptors);
			return (uint32)SteamAPI_ISteamUGC_GetUserContentDescriptorPreferences(self, ptr0, cMaxEntries); }, py::arg("pvecDescriptors"), py::arg("cMaxEntries"))
 		.def("SetItemsDisabledLocally", [](ISteamUGC* self, uintptr_t pvecPublishedFileIDs, uint32 unNumPublishedFileIDs, bool bDisabledLocally) { 
			PublishedFileId_t * ptr0 = reinterpret_cast<PublishedFileId_t *>(pvecPublishedFileIDs);
			return (bool)SteamAPI_ISteamUGC_SetItemsDisabledLocally(self, ptr0, unNumPublishedFileIDs, bDisabledLocally); }, py::arg("pvecPublishedFileIDs"), py::arg("unNumPublishedFileIDs"), py::arg("bDisabledLocally"))
 		.def("SetSubscriptionsLoadOrder", [](ISteamUGC* self, uintptr_t pvecPublishedFileIDs, uint32 unNumPublishedFileIDs) { 
			PublishedFileId_t * ptr0 = reinterpret_cast<PublishedFileId_t *>(pvecPublishedFileIDs);
			return (bool)SteamAPI_ISteamUGC_SetSubscriptionsLoadOrder(self, ptr0, unNumPublishedFileIDs); }, py::arg("pvecPublishedFileIDs"), py::arg("unNumPublishedFileIDs"))
 		.def("MarkDownloadedItemAsUnused", &SteamAPI_ISteamUGC_MarkDownloadedItemAsUnused, py::arg("nPublishedFileID"))
 		.def("GetNumDownloadedItems", &SteamAPI_ISteamUGC_GetNumDownloadedItems)
 		.def("GetDownloadedItems", [](ISteamUGC* self, uintptr_t pvecPublishedFileIDs, uint32 cMaxEntries) { 
			PublishedFileId_t * ptr0 = reinterpret_cast<PublishedFileId_t *>(pvecPublishedFileIDs);
			return (uint32)SteamAPI_ISteamUGC_GetDownloadedItems(self, ptr0, cMaxEntries); }, py::arg("pvecPublishedFileIDs"), py::arg("cMaxEntries"))
 	;
 	m.def("SteamHTMLSurface",[]() -> ISteamHTMLSurface* { return SteamHTMLSurface(); }, py::return_value_policy::reference);
 	py::class_<ISteamHTMLSurface, std::unique_ptr<ISteamHTMLSurface, py::nodelete>> _isteamhtmlsurface(m, "ISteamHTMLSurface");
 	_isteamhtmlsurface
 		.def_property_readonly("ptr", [](ISteamHTMLSurface& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def("Init", &SteamAPI_ISteamHTMLSurface_Init)
 		.def("Shutdown", &SteamAPI_ISteamHTMLSurface_Shutdown)
 		.def("CreateBrowser", [](ISteamHTMLSurface* self, uintptr_t pchUserAgent, uintptr_t pchUserCSS) { 
			const char * ptr0 = reinterpret_cast<const char *>(pchUserAgent);
			const char * ptr1 = reinterpret_cast<const char *>(pchUserCSS);
			return (SteamAPICall_t)SteamAPI_ISteamHTMLSurface_CreateBrowser(self, ptr0, ptr1); }, py::arg("pchUserAgent"), py::arg("pchUserCSS"))
 		.def("RemoveBrowser", &SteamAPI_ISteamHTMLSurface_RemoveBrowser, py::arg("unBrowserHandle"))
 		.def("LoadURL", [](ISteamHTMLSurface* self, HHTMLBrowser unBrowserHandle, uintptr_t pchURL, uintptr_t pchPostData) { 
			const char * ptr1 = reinterpret_cast<const char *>(pchURL);
			const char * ptr2 = reinterpret_cast<const char *>(pchPostData);
			SteamAPI_ISteamHTMLSurface_LoadURL(self, unBrowserHandle, ptr1, ptr2); }, py::arg("unBrowserHandle"), py::arg("pchURL"), py::arg("pchPostData"))
 		.def("SetSize", &SteamAPI_ISteamHTMLSurface_SetSize, py::arg("unBrowserHandle"), py::arg("unWidth"), py::arg("unHeight"))
 		.def("StopLoad", &SteamAPI_ISteamHTMLSurface_StopLoad, py::arg("unBrowserHandle"))
 		.def("Reload", &SteamAPI_ISteamHTMLSurface_Reload, py::arg("unBrowserHandle"))
 		.def("GoBack", &SteamAPI_ISteamHTMLSurface_GoBack, py::arg("unBrowserHandle"))
 		.def("GoForward", &SteamAPI_ISteamHTMLSurface_GoForward, py::arg("unBrowserHandle"))
 		.def("AddHeader", [](ISteamHTMLSurface* self, HHTMLBrowser unBrowserHandle, uintptr_t pchKey, uintptr_t pchValue) { 
			const char * ptr1 = reinterpret_cast<const char *>(pchKey);
			const char * ptr2 = reinterpret_cast<const char *>(pchValue);
			SteamAPI_ISteamHTMLSurface_AddHeader(self, unBrowserHandle, ptr1, ptr2); }, py::arg("unBrowserHandle"), py::arg("pchKey"), py::arg("pchValue"))
 		.def("ExecuteJavascript", [](ISteamHTMLSurface* self, HHTMLBrowser unBrowserHandle, uintptr_t pchScript) { 
			const char * ptr1 = reinterpret_cast<const char *>(pchScript);
			SteamAPI_ISteamHTMLSurface_ExecuteJavascript(self, unBrowserHandle, ptr1); }, py::arg("unBrowserHandle"), py::arg("pchScript"))
 		.def("MouseUp", &SteamAPI_ISteamHTMLSurface_MouseUp, py::arg("unBrowserHandle"), py::arg("eMouseButton"))
 		.def("MouseDown", &SteamAPI_ISteamHTMLSurface_MouseDown, py::arg("unBrowserHandle"), py::arg("eMouseButton"))
 		.def("MouseDoubleClick", &SteamAPI_ISteamHTMLSurface_MouseDoubleClick, py::arg("unBrowserHandle"), py::arg("eMouseButton"))
 		.def("MouseMove", &SteamAPI_ISteamHTMLSurface_MouseMove, py::arg("unBrowserHandle"), py::arg("x"), py::arg("y"))
 		.def("MouseWheel", &SteamAPI_ISteamHTMLSurface_MouseWheel, py::arg("unBrowserHandle"), py::arg("nDelta"))
 		.def("KeyDown", &SteamAPI_ISteamHTMLSurface_KeyDown, py::arg("unBrowserHandle"), py::arg("nNativeKeyCode"), py::arg("eHTMLKeyModifiers"), py::arg("bIsSystemKey"))
 		.def("KeyUp", &SteamAPI_ISteamHTMLSurface_KeyUp, py::arg("unBrowserHandle"), py::arg("nNativeKeyCode"), py::arg("eHTMLKeyModifiers"))
 		.def("KeyChar", &SteamAPI_ISteamHTMLSurface_KeyChar, py::arg("unBrowserHandle"), py::arg("cUnicodeChar"), py::arg("eHTMLKeyModifiers"))
 		.def("SetHorizontalScroll", &SteamAPI_ISteamHTMLSurface_SetHorizontalScroll, py::arg("unBrowserHandle"), py::arg("nAbsolutePixelScroll"))
 		.def("SetVerticalScroll", &SteamAPI_ISteamHTMLSurface_SetVerticalScroll, py::arg("unBrowserHandle"), py::arg("nAbsolutePixelScroll"))
 		.def("SetKeyFocus", &SteamAPI_ISteamHTMLSurface_SetKeyFocus, py::arg("unBrowserHandle"), py::arg("bHasKeyFocus"))
 		.def("ViewSource", &SteamAPI_ISteamHTMLSurface_ViewSource, py::arg("unBrowserHandle"))
 		.def("CopyToClipboard", &SteamAPI_ISteamHTMLSurface_CopyToClipboard, py::arg("unBrowserHandle"))
 		.def("PasteFromClipboard", &SteamAPI_ISteamHTMLSurface_PasteFromClipboard, py::arg("unBrowserHandle"))
 		.def("Find", [](ISteamHTMLSurface* self, HHTMLBrowser unBrowserHandle, uintptr_t pchSearchStr, bool bCurrentlyInFind, bool bReverse) { 
			const char * ptr1 = reinterpret_cast<const char *>(pchSearchStr);
			SteamAPI_ISteamHTMLSurface_Find(self, unBrowserHandle, ptr1, bCurrentlyInFind, bReverse); }, py::arg("unBrowserHandle"), py::arg("pchSearchStr"), py::arg("bCurrentlyInFind"), py::arg("bReverse"))
 		.def("StopFind", &SteamAPI_ISteamHTMLSurface_StopFind, py::arg("unBrowserHandle"))
 		.def("GetLinkAtPosition", &SteamAPI_ISteamHTMLSurface_GetLinkAtPosition, py::arg("unBrowserHandle"), py::arg("x"), py::arg("y"))
 		.def("SetCookie", [](ISteamHTMLSurface* self, uintptr_t pchHostname, uintptr_t pchKey, uintptr_t pchValue, uintptr_t pchPath, RTime32 nExpires, bool bSecure, bool bHTTPOnly) { 
			const char * ptr0 = reinterpret_cast<const char *>(pchHostname);
			const char * ptr1 = reinterpret_cast<const char *>(pchKey);
			const char * ptr2 = reinterpret_cast<const char *>(pchValue);
			const char * ptr3 = reinterpret_cast<const char *>(pchPath);
			SteamAPI_ISteamHTMLSurface_SetCookie(self, ptr0, ptr1, ptr2, ptr3, nExpires, bSecure, bHTTPOnly); }, py::arg("pchHostname"), py::arg("pchKey"), py::arg("pchValue"), py::arg("pchPath"), py::arg("nExpires"), py::arg("bSecure"), py::arg("bHTTPOnly"))
 		.def("SetPageScaleFactor", &SteamAPI_ISteamHTMLSurface_SetPageScaleFactor, py::arg("unBrowserHandle"), py::arg("flZoom"), py::arg("nPointX"), py::arg("nPointY"))
 		.def("SetBackgroundMode", &SteamAPI_ISteamHTMLSurface_SetBackgroundMode, py::arg("unBrowserHandle"), py::arg("bBackgroundMode"))
 		.def("SetDPIScalingFactor", &SteamAPI_ISteamHTMLSurface_SetDPIScalingFactor, py::arg("unBrowserHandle"), py::arg("flDPIScaling"))
 		.def("OpenDeveloperTools", &SteamAPI_ISteamHTMLSurface_OpenDeveloperTools, py::arg("unBrowserHandle"))
 		.def("AllowStartRequest", &SteamAPI_ISteamHTMLSurface_AllowStartRequest, py::arg("unBrowserHandle"), py::arg("bAllowed"))
 		.def("JSDialogResponse", &SteamAPI_ISteamHTMLSurface_JSDialogResponse, py::arg("unBrowserHandle"), py::arg("bResult"))
 		.def("FileLoadDialogResponse", [](ISteamHTMLSurface* self, HHTMLBrowser unBrowserHandle, uintptr_t pchSelectedFiles) { 
			const char ** ptr1 = reinterpret_cast<const char **>(pchSelectedFiles);
			SteamAPI_ISteamHTMLSurface_FileLoadDialogResponse(self, unBrowserHandle, ptr1); }, py::arg("unBrowserHandle"), py::arg("pchSelectedFiles"))
 	;
 	m.def("SteamInventory",[]() -> ISteamInventory* { return SteamInventory(); }, py::return_value_policy::reference);
 	py::class_<ISteamInventory, std::unique_ptr<ISteamInventory, py::nodelete>> _isteaminventory(m, "ISteamInventory");
 	_isteaminventory
 		.def_property_readonly("ptr", [](ISteamInventory& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def("GetResultStatus", &SteamAPI_ISteamInventory_GetResultStatus, py::arg("resultHandle"))
 		.def("GetResultItems", [](ISteamInventory* self, SteamInventoryResult_t resultHandle, uintptr_t pOutItemsArray, uintptr_t punOutItemsArraySize) { 
			SteamItemDetails_t * ptr1 = reinterpret_cast<SteamItemDetails_t *>(pOutItemsArray);
			uint32 * ptr2 = reinterpret_cast<uint32 *>(punOutItemsArraySize);
			return (bool)SteamAPI_ISteamInventory_GetResultItems(self, resultHandle, ptr1, ptr2); }, py::arg("resultHandle"), py::arg("pOutItemsArray"), py::arg("punOutItemsArraySize"))
 		.def("GetResultItemProperty", [](ISteamInventory* self, SteamInventoryResult_t resultHandle, uint32 unItemIndex, uintptr_t pchPropertyName, uintptr_t pchValueBuffer, uintptr_t punValueBufferSizeOut) { 
			const char * ptr2 = reinterpret_cast<const char *>(pchPropertyName);
			char * ptr3 = reinterpret_cast<char *>(pchValueBuffer);
			uint32 * ptr4 = reinterpret_cast<uint32 *>(punValueBufferSizeOut);
			return (bool)SteamAPI_ISteamInventory_GetResultItemProperty(self, resultHandle, unItemIndex, ptr2, ptr3, ptr4); }, py::arg("resultHandle"), py::arg("unItemIndex"), py::arg("pchPropertyName"), py::arg("pchValueBuffer"), py::arg("punValueBufferSizeOut"))
 		.def("GetResultTimestamp", &SteamAPI_ISteamInventory_GetResultTimestamp, py::arg("resultHandle"))
 		.def("CheckResultSteamID", &SteamAPI_ISteamInventory_CheckResultSteamID, py::arg("resultHandle"), py::arg("steamIDExpected"))
 		.def("DestroyResult", &SteamAPI_ISteamInventory_DestroyResult, py::arg("resultHandle"))
 		.def("GetAllItems", [](ISteamInventory* self, uintptr_t pResultHandle) { 
			SteamInventoryResult_t * ptr0 = reinterpret_cast<SteamInventoryResult_t *>(pResultHandle);
			return (bool)SteamAPI_ISteamInventory_GetAllItems(self, ptr0); }, py::arg("pResultHandle"))
 		.def("GetItemsByID", [](ISteamInventory* self, uintptr_t pResultHandle, uintptr_t pInstanceIDs, uint32 unCountInstanceIDs) { 
			SteamInventoryResult_t * ptr0 = reinterpret_cast<SteamInventoryResult_t *>(pResultHandle);
			const SteamItemInstanceID_t * ptr1 = reinterpret_cast<const SteamItemInstanceID_t *>(pInstanceIDs);
			return (bool)SteamAPI_ISteamInventory_GetItemsByID(self, ptr0, ptr1, unCountInstanceIDs); }, py::arg("pResultHandle"), py::arg("pInstanceIDs"), py::arg("unCountInstanceIDs"))
 		.def("SerializeResult", [](ISteamInventory* self, SteamInventoryResult_t resultHandle, uintptr_t pOutBuffer, uintptr_t punOutBufferSize) { 
			void * ptr1 = reinterpret_cast<void *>(pOutBuffer);
			uint32 * ptr2 = reinterpret_cast<uint32 *>(punOutBufferSize);
			return (bool)SteamAPI_ISteamInventory_SerializeResult(self, resultHandle, ptr1, ptr2); }, py::arg("resultHandle"), py::arg("pOutBuffer"), py::arg("punOutBufferSize"))
 		.def("DeserializeResult", [](ISteamInventory* self, uintptr_t pOutResultHandle, uintptr_t pBuffer, uint32 unBufferSize, bool bRESERVED_MUST_BE_FALSE) { 
			SteamInventoryResult_t * ptr0 = reinterpret_cast<SteamInventoryResult_t *>(pOutResultHandle);
			const void * ptr1 = reinterpret_cast<const void *>(pBuffer);
			return (bool)SteamAPI_ISteamInventory_DeserializeResult(self, ptr0, ptr1, unBufferSize, bRESERVED_MUST_BE_FALSE); }, py::arg("pOutResultHandle"), py::arg("pBuffer"), py::arg("unBufferSize"), py::arg("bRESERVED_MUST_BE_FALSE"))
 		.def("GenerateItems", [](ISteamInventory* self, uintptr_t pResultHandle, uintptr_t pArrayItemDefs, uintptr_t punArrayQuantity, uint32 unArrayLength) { 
			SteamInventoryResult_t * ptr0 = reinterpret_cast<SteamInventoryResult_t *>(pResultHandle);
			const SteamItemDef_t * ptr1 = reinterpret_cast<const SteamItemDef_t *>(pArrayItemDefs);
			const uint32 * ptr2 = reinterpret_cast<const uint32 *>(punArrayQuantity);
			return (bool)SteamAPI_ISteamInventory_GenerateItems(self, ptr0, ptr1, ptr2, unArrayLength); }, py::arg("pResultHandle"), py::arg("pArrayItemDefs"), py::arg("punArrayQuantity"), py::arg("unArrayLength"))
 		.def("GrantPromoItems", [](ISteamInventory* self, uintptr_t pResultHandle) { 
			SteamInventoryResult_t * ptr0 = reinterpret_cast<SteamInventoryResult_t *>(pResultHandle);
			return (bool)SteamAPI_ISteamInventory_GrantPromoItems(self, ptr0); }, py::arg("pResultHandle"))
 		.def("AddPromoItem", [](ISteamInventory* self, uintptr_t pResultHandle, SteamItemDef_t itemDef) { 
			SteamInventoryResult_t * ptr0 = reinterpret_cast<SteamInventoryResult_t *>(pResultHandle);
			return (bool)SteamAPI_ISteamInventory_AddPromoItem(self, ptr0, itemDef); }, py::arg("pResultHandle"), py::arg("itemDef"))
 		.def("AddPromoItems", [](ISteamInventory* self, uintptr_t pResultHandle, uintptr_t pArrayItemDefs, uint32 unArrayLength) { 
			SteamInventoryResult_t * ptr0 = reinterpret_cast<SteamInventoryResult_t *>(pResultHandle);
			const SteamItemDef_t * ptr1 = reinterpret_cast<const SteamItemDef_t *>(pArrayItemDefs);
			return (bool)SteamAPI_ISteamInventory_AddPromoItems(self, ptr0, ptr1, unArrayLength); }, py::arg("pResultHandle"), py::arg("pArrayItemDefs"), py::arg("unArrayLength"))
 		.def("ConsumeItem", [](ISteamInventory* self, uintptr_t pResultHandle, SteamItemInstanceID_t itemConsume, uint32 unQuantity) { 
			SteamInventoryResult_t * ptr0 = reinterpret_cast<SteamInventoryResult_t *>(pResultHandle);
			return (bool)SteamAPI_ISteamInventory_ConsumeItem(self, ptr0, itemConsume, unQuantity); }, py::arg("pResultHandle"), py::arg("itemConsume"), py::arg("unQuantity"))
 		.def("ExchangeItems", [](ISteamInventory* self, uintptr_t pResultHandle, uintptr_t pArrayGenerate, uintptr_t punArrayGenerateQuantity, uint32 unArrayGenerateLength, uintptr_t pArrayDestroy, uintptr_t punArrayDestroyQuantity, uint32 unArrayDestroyLength) { 
			SteamInventoryResult_t * ptr0 = reinterpret_cast<SteamInventoryResult_t *>(pResultHandle);
			const SteamItemDef_t * ptr1 = reinterpret_cast<const SteamItemDef_t *>(pArrayGenerate);
			const uint32 * ptr2 = reinterpret_cast<const uint32 *>(punArrayGenerateQuantity);
			const SteamItemInstanceID_t * ptr4 = reinterpret_cast<const SteamItemInstanceID_t *>(pArrayDestroy);
			const uint32 * ptr5 = reinterpret_cast<const uint32 *>(punArrayDestroyQuantity);
			return (bool)SteamAPI_ISteamInventory_ExchangeItems(self, ptr0, ptr1, ptr2, unArrayGenerateLength, ptr4, ptr5, unArrayDestroyLength); }, py::arg("pResultHandle"), py::arg("pArrayGenerate"), py::arg("punArrayGenerateQuantity"), py::arg("unArrayGenerateLength"), py::arg("pArrayDestroy"), py::arg("punArrayDestroyQuantity"), py::arg("unArrayDestroyLength"))
 		.def("TransferItemQuantity", [](ISteamInventory* self, uintptr_t pResultHandle, SteamItemInstanceID_t itemIdSource, uint32 unQuantity, SteamItemInstanceID_t itemIdDest) { 
			SteamInventoryResult_t * ptr0 = reinterpret_cast<SteamInventoryResult_t *>(pResultHandle);
			return (bool)SteamAPI_ISteamInventory_TransferItemQuantity(self, ptr0, itemIdSource, unQuantity, itemIdDest); }, py::arg("pResultHandle"), py::arg("itemIdSource"), py::arg("unQuantity"), py::arg("itemIdDest"))
 		.def("SendItemDropHeartbeat", &SteamAPI_ISteamInventory_SendItemDropHeartbeat)
 		.def("TriggerItemDrop", [](ISteamInventory* self, uintptr_t pResultHandle, SteamItemDef_t dropListDefinition) { 
			SteamInventoryResult_t * ptr0 = reinterpret_cast<SteamInventoryResult_t *>(pResultHandle);
			return (bool)SteamAPI_ISteamInventory_TriggerItemDrop(self, ptr0, dropListDefinition); }, py::arg("pResultHandle"), py::arg("dropListDefinition"))
 		.def("TradeItems", [](ISteamInventory* self, uintptr_t pResultHandle, uint64_steamid steamIDTradePartner, uintptr_t pArrayGive, uintptr_t pArrayGiveQuantity, uint32 nArrayGiveLength, uintptr_t pArrayGet, uintptr_t pArrayGetQuantity, uint32 nArrayGetLength) { 
			SteamInventoryResult_t * ptr0 = reinterpret_cast<SteamInventoryResult_t *>(pResultHandle);
			const SteamItemInstanceID_t * ptr2 = reinterpret_cast<const SteamItemInstanceID_t *>(pArrayGive);
			const uint32 * ptr3 = reinterpret_cast<const uint32 *>(pArrayGiveQuantity);
			const SteamItemInstanceID_t * ptr5 = reinterpret_cast<const SteamItemInstanceID_t *>(pArrayGet);
			const uint32 * ptr6 = reinterpret_cast<const uint32 *>(pArrayGetQuantity);
			return (bool)SteamAPI_ISteamInventory_TradeItems(self, ptr0, steamIDTradePartner, ptr2, ptr3, nArrayGiveLength, ptr5, ptr6, nArrayGetLength); }, py::arg("pResultHandle"), py::arg("steamIDTradePartner"), py::arg("pArrayGive"), py::arg("pArrayGiveQuantity"), py::arg("nArrayGiveLength"), py::arg("pArrayGet"), py::arg("pArrayGetQuantity"), py::arg("nArrayGetLength"))
 		.def("LoadItemDefinitions", &SteamAPI_ISteamInventory_LoadItemDefinitions)
 		.def("GetItemDefinitionIDs", [](ISteamInventory* self, uintptr_t pItemDefIDs, uintptr_t punItemDefIDsArraySize) { 
			SteamItemDef_t * ptr0 = reinterpret_cast<SteamItemDef_t *>(pItemDefIDs);
			uint32 * ptr1 = reinterpret_cast<uint32 *>(punItemDefIDsArraySize);
			return (bool)SteamAPI_ISteamInventory_GetItemDefinitionIDs(self, ptr0, ptr1); }, py::arg("pItemDefIDs"), py::arg("punItemDefIDsArraySize"))
 		.def("GetItemDefinitionProperty", [](ISteamInventory* self, SteamItemDef_t iDefinition, uintptr_t pchPropertyName, uintptr_t pchValueBuffer, uintptr_t punValueBufferSizeOut) { 
			const char * ptr1 = reinterpret_cast<const char *>(pchPropertyName);
			char * ptr2 = reinterpret_cast<char *>(pchValueBuffer);
			uint32 * ptr3 = reinterpret_cast<uint32 *>(punValueBufferSizeOut);
			return (bool)SteamAPI_ISteamInventory_GetItemDefinitionProperty(self, iDefinition, ptr1, ptr2, ptr3); }, py::arg("iDefinition"), py::arg("pchPropertyName"), py::arg("pchValueBuffer"), py::arg("punValueBufferSizeOut"))
 		.def("RequestEligiblePromoItemDefinitionsIDs", &SteamAPI_ISteamInventory_RequestEligiblePromoItemDefinitionsIDs, py::arg("steamID"))
 		.def("GetEligiblePromoItemDefinitionIDs", [](ISteamInventory* self, uint64_steamid steamID, uintptr_t pItemDefIDs, uintptr_t punItemDefIDsArraySize) { 
			SteamItemDef_t * ptr1 = reinterpret_cast<SteamItemDef_t *>(pItemDefIDs);
			uint32 * ptr2 = reinterpret_cast<uint32 *>(punItemDefIDsArraySize);
			return (bool)SteamAPI_ISteamInventory_GetEligiblePromoItemDefinitionIDs(self, steamID, ptr1, ptr2); }, py::arg("steamID"), py::arg("pItemDefIDs"), py::arg("punItemDefIDsArraySize"))
 		.def("StartPurchase", [](ISteamInventory* self, uintptr_t pArrayItemDefs, uintptr_t punArrayQuantity, uint32 unArrayLength) { 
			const SteamItemDef_t * ptr0 = reinterpret_cast<const SteamItemDef_t *>(pArrayItemDefs);
			const uint32 * ptr1 = reinterpret_cast<const uint32 *>(punArrayQuantity);
			return (SteamAPICall_t)SteamAPI_ISteamInventory_StartPurchase(self, ptr0, ptr1, unArrayLength); }, py::arg("pArrayItemDefs"), py::arg("punArrayQuantity"), py::arg("unArrayLength"))
 		.def("RequestPrices", &SteamAPI_ISteamInventory_RequestPrices)
 		.def("GetNumItemsWithPrices", &SteamAPI_ISteamInventory_GetNumItemsWithPrices)
 		.def("GetItemsWithPrices", [](ISteamInventory* self, uintptr_t pArrayItemDefs, uintptr_t pCurrentPrices, uintptr_t pBasePrices, uint32 unArrayLength) { 
			SteamItemDef_t * ptr0 = reinterpret_cast<SteamItemDef_t *>(pArrayItemDefs);
			uint64 * ptr1 = reinterpret_cast<uint64 *>(pCurrentPrices);
			uint64 * ptr2 = reinterpret_cast<uint64 *>(pBasePrices);
			return (bool)SteamAPI_ISteamInventory_GetItemsWithPrices(self, ptr0, ptr1, ptr2, unArrayLength); }, py::arg("pArrayItemDefs"), py::arg("pCurrentPrices"), py::arg("pBasePrices"), py::arg("unArrayLength"))
 		.def("GetItemPrice", [](ISteamInventory* self, SteamItemDef_t iDefinition, uintptr_t pCurrentPrice, uintptr_t pBasePrice) { 
			uint64 * ptr1 = reinterpret_cast<uint64 *>(pCurrentPrice);
			uint64 * ptr2 = reinterpret_cast<uint64 *>(pBasePrice);
			return (bool)SteamAPI_ISteamInventory_GetItemPrice(self, iDefinition, ptr1, ptr2); }, py::arg("iDefinition"), py::arg("pCurrentPrice"), py::arg("pBasePrice"))
 		.def("StartUpdateProperties", &SteamAPI_ISteamInventory_StartUpdateProperties)
 		.def("RemoveProperty", [](ISteamInventory* self, SteamInventoryUpdateHandle_t handle, SteamItemInstanceID_t nItemID, uintptr_t pchPropertyName) { 
			const char * ptr2 = reinterpret_cast<const char *>(pchPropertyName);
			return (bool)SteamAPI_ISteamInventory_RemoveProperty(self, handle, nItemID, ptr2); }, py::arg("handle"), py::arg("nItemID"), py::arg("pchPropertyName"))
 		.def("SetProperty", [](ISteamInventory* self, SteamInventoryUpdateHandle_t handle, SteamItemInstanceID_t nItemID, uintptr_t pchPropertyName, float flValue) { 
			const char * ptr2 = reinterpret_cast<const char *>(pchPropertyName);
			return (bool)SteamAPI_ISteamInventory_SetPropertyFloat(self, handle, nItemID, ptr2, flValue); }, py::arg("handle"), py::arg("nItemID"), py::arg("pchPropertyName"), py::arg("flValue"))
 		.def("SubmitUpdateProperties", [](ISteamInventory* self, SteamInventoryUpdateHandle_t handle, uintptr_t pResultHandle) { 
			SteamInventoryResult_t * ptr1 = reinterpret_cast<SteamInventoryResult_t *>(pResultHandle);
			return (bool)SteamAPI_ISteamInventory_SubmitUpdateProperties(self, handle, ptr1); }, py::arg("handle"), py::arg("pResultHandle"))
 		.def("InspectItem", [](ISteamInventory* self, uintptr_t pResultHandle, uintptr_t pchItemToken) { 
			SteamInventoryResult_t * ptr0 = reinterpret_cast<SteamInventoryResult_t *>(pResultHandle);
			const char * ptr1 = reinterpret_cast<const char *>(pchItemToken);
			return (bool)SteamAPI_ISteamInventory_InspectItem(self, ptr0, ptr1); }, py::arg("pResultHandle"), py::arg("pchItemToken"))
 	;
 	m.def("SteamTimeline",[]() -> ISteamTimeline* { return SteamTimeline(); }, py::return_value_policy::reference);
 	py::class_<ISteamTimeline, std::unique_ptr<ISteamTimeline, py::nodelete>> _isteamtimeline(m, "ISteamTimeline");
 	_isteamtimeline
 		.def_property_readonly("ptr", [](ISteamTimeline& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def("SetTimelineTooltip", [](ISteamTimeline* self, uintptr_t pchDescription, float flTimeDelta) { 
			const char * ptr0 = reinterpret_cast<const char *>(pchDescription);
			SteamAPI_ISteamTimeline_SetTimelineTooltip(self, ptr0, flTimeDelta); }, py::arg("pchDescription"), py::arg("flTimeDelta"))
 		.def("ClearTimelineTooltip", &SteamAPI_ISteamTimeline_ClearTimelineTooltip, py::arg("flTimeDelta"))
 		.def("SetTimelineGameMode", &SteamAPI_ISteamTimeline_SetTimelineGameMode, py::arg("eMode"))
 		.def("AddInstantaneousTimelineEvent", [](ISteamTimeline* self, uintptr_t pchTitle, uintptr_t pchDescription, uintptr_t pchIcon, uint32 unIconPriority, float flStartOffsetSeconds, ETimelineEventClipPriority ePossibleClip) { 
			const char * ptr0 = reinterpret_cast<const char *>(pchTitle);
			const char * ptr1 = reinterpret_cast<const char *>(pchDescription);
			const char * ptr2 = reinterpret_cast<const char *>(pchIcon);
			return (TimelineEventHandle_t)SteamAPI_ISteamTimeline_AddInstantaneousTimelineEvent(self, ptr0, ptr1, ptr2, unIconPriority, flStartOffsetSeconds, ePossibleClip); }, py::arg("pchTitle"), py::arg("pchDescription"), py::arg("pchIcon"), py::arg("unIconPriority"), py::arg("flStartOffsetSeconds"), py::arg("ePossibleClip"))
 		.def("AddRangeTimelineEvent", [](ISteamTimeline* self, uintptr_t pchTitle, uintptr_t pchDescription, uintptr_t pchIcon, uint32 unIconPriority, float flStartOffsetSeconds, float flDuration, ETimelineEventClipPriority ePossibleClip) { 
			const char * ptr0 = reinterpret_cast<const char *>(pchTitle);
			const char * ptr1 = reinterpret_cast<const char *>(pchDescription);
			const char * ptr2 = reinterpret_cast<const char *>(pchIcon);
			return (TimelineEventHandle_t)SteamAPI_ISteamTimeline_AddRangeTimelineEvent(self, ptr0, ptr1, ptr2, unIconPriority, flStartOffsetSeconds, flDuration, ePossibleClip); }, py::arg("pchTitle"), py::arg("pchDescription"), py::arg("pchIcon"), py::arg("unIconPriority"), py::arg("flStartOffsetSeconds"), py::arg("flDuration"), py::arg("ePossibleClip"))
 		.def("StartRangeTimelineEvent", [](ISteamTimeline* self, uintptr_t pchTitle, uintptr_t pchDescription, uintptr_t pchIcon, uint32 unPriority, float flStartOffsetSeconds, ETimelineEventClipPriority ePossibleClip) { 
			const char * ptr0 = reinterpret_cast<const char *>(pchTitle);
			const char * ptr1 = reinterpret_cast<const char *>(pchDescription);
			const char * ptr2 = reinterpret_cast<const char *>(pchIcon);
			return (TimelineEventHandle_t)SteamAPI_ISteamTimeline_StartRangeTimelineEvent(self, ptr0, ptr1, ptr2, unPriority, flStartOffsetSeconds, ePossibleClip); }, py::arg("pchTitle"), py::arg("pchDescription"), py::arg("pchIcon"), py::arg("unPriority"), py::arg("flStartOffsetSeconds"), py::arg("ePossibleClip"))
 		.def("UpdateRangeTimelineEvent", [](ISteamTimeline* self, TimelineEventHandle_t ulEvent, uintptr_t pchTitle, uintptr_t pchDescription, uintptr_t pchIcon, uint32 unPriority, ETimelineEventClipPriority ePossibleClip) { 
			const char * ptr1 = reinterpret_cast<const char *>(pchTitle);
			const char * ptr2 = reinterpret_cast<const char *>(pchDescription);
			const char * ptr3 = reinterpret_cast<const char *>(pchIcon);
			SteamAPI_ISteamTimeline_UpdateRangeTimelineEvent(self, ulEvent, ptr1, ptr2, ptr3, unPriority, ePossibleClip); }, py::arg("ulEvent"), py::arg("pchTitle"), py::arg("pchDescription"), py::arg("pchIcon"), py::arg("unPriority"), py::arg("ePossibleClip"))
 		.def("EndRangeTimelineEvent", &SteamAPI_ISteamTimeline_EndRangeTimelineEvent, py::arg("ulEvent"), py::arg("flEndOffsetSeconds"))
 		.def("RemoveTimelineEvent", &SteamAPI_ISteamTimeline_RemoveTimelineEvent, py::arg("ulEvent"))
 		.def("DoesEventRecordingExist", &SteamAPI_ISteamTimeline_DoesEventRecordingExist, py::arg("ulEvent"))
 		.def("StartGamePhase", &SteamAPI_ISteamTimeline_StartGamePhase)
 		.def("EndGamePhase", &SteamAPI_ISteamTimeline_EndGamePhase)
 		.def("SetGamePhaseID", [](ISteamTimeline* self, uintptr_t pchPhaseID) { 
			const char * ptr0 = reinterpret_cast<const char *>(pchPhaseID);
			SteamAPI_ISteamTimeline_SetGamePhaseID(self, ptr0); }, py::arg("pchPhaseID"))
 		.def("DoesGamePhaseRecordingExist", [](ISteamTimeline* self, uintptr_t pchPhaseID) { 
			const char * ptr0 = reinterpret_cast<const char *>(pchPhaseID);
			return (SteamAPICall_t)SteamAPI_ISteamTimeline_DoesGamePhaseRecordingExist(self, ptr0); }, py::arg("pchPhaseID"))
 		.def("AddGamePhaseTag", [](ISteamTimeline* self, uintptr_t pchTagName, uintptr_t pchTagIcon, uintptr_t pchTagGroup, uint32 unPriority) { 
			const char * ptr0 = reinterpret_cast<const char *>(pchTagName);
			const char * ptr1 = reinterpret_cast<const char *>(pchTagIcon);
			const char * ptr2 = reinterpret_cast<const char *>(pchTagGroup);
			SteamAPI_ISteamTimeline_AddGamePhaseTag(self, ptr0, ptr1, ptr2, unPriority); }, py::arg("pchTagName"), py::arg("pchTagIcon"), py::arg("pchTagGroup"), py::arg("unPriority"))
 		.def("SetGamePhaseAttribute", [](ISteamTimeline* self, uintptr_t pchAttributeGroup, uintptr_t pchAttributeValue, uint32 unPriority) { 
			const char * ptr0 = reinterpret_cast<const char *>(pchAttributeGroup);
			const char * ptr1 = reinterpret_cast<const char *>(pchAttributeValue);
			SteamAPI_ISteamTimeline_SetGamePhaseAttribute(self, ptr0, ptr1, unPriority); }, py::arg("pchAttributeGroup"), py::arg("pchAttributeValue"), py::arg("unPriority"))
 		.def("OpenOverlayToGamePhase", [](ISteamTimeline* self, uintptr_t pchPhaseID) { 
			const char * ptr0 = reinterpret_cast<const char *>(pchPhaseID);
			SteamAPI_ISteamTimeline_OpenOverlayToGamePhase(self, ptr0); }, py::arg("pchPhaseID"))
 		.def("OpenOverlayToTimelineEvent", &SteamAPI_ISteamTimeline_OpenOverlayToTimelineEvent, py::arg("ulEvent"))
 	;
 	m.def("SteamVideo",[]() -> ISteamVideo* { return SteamVideo(); }, py::return_value_policy::reference);
 	py::class_<ISteamVideo, std::unique_ptr<ISteamVideo, py::nodelete>> _isteamvideo(m, "ISteamVideo");
 	_isteamvideo
 		.def_property_readonly("ptr", [](ISteamVideo& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def("GetVideoURL", &SteamAPI_ISteamVideo_GetVideoURL, py::arg("unVideoAppID"))
 		.def("IsBroadcasting", [](ISteamVideo* self, uintptr_t pnNumViewers) { 
			int * ptr0 = reinterpret_cast<int *>(pnNumViewers);
			return (bool)SteamAPI_ISteamVideo_IsBroadcasting(self, ptr0); }, py::arg("pnNumViewers"))
 		.def("GetOPFSettings", &SteamAPI_ISteamVideo_GetOPFSettings, py::arg("unVideoAppID"))
 		.def("GetOPFStringForApp", [](ISteamVideo* self, AppId_t unVideoAppID, uintptr_t pchBuffer, uintptr_t pnBufferSize) { 
			char * ptr1 = reinterpret_cast<char *>(pchBuffer);
			int32 * ptr2 = reinterpret_cast<int32 *>(pnBufferSize);
			return (bool)SteamAPI_ISteamVideo_GetOPFStringForApp(self, unVideoAppID, ptr1, ptr2); }, py::arg("unVideoAppID"), py::arg("pchBuffer"), py::arg("pnBufferSize"))
 	;
 	m.def("SteamParentalSettings",[]() -> ISteamParentalSettings* { return SteamParentalSettings(); }, py::return_value_policy::reference);
 	py::class_<ISteamParentalSettings, std::unique_ptr<ISteamParentalSettings, py::nodelete>> _isteamparentalsettings(m, "ISteamParentalSettings");
 	_isteamparentalsettings
 		.def_property_readonly("ptr", [](ISteamParentalSettings& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def("BIsParentalLockEnabled", &SteamAPI_ISteamParentalSettings_BIsParentalLockEnabled)
 		.def("BIsParentalLockLocked", &SteamAPI_ISteamParentalSettings_BIsParentalLockLocked)
 		.def("BIsAppBlocked", &SteamAPI_ISteamParentalSettings_BIsAppBlocked, py::arg("nAppID"))
 		.def("BIsAppInBlockList", &SteamAPI_ISteamParentalSettings_BIsAppInBlockList, py::arg("nAppID"))
 		.def("BIsFeatureBlocked", &SteamAPI_ISteamParentalSettings_BIsFeatureBlocked, py::arg("eFeature"))
 		.def("BIsFeatureInBlockList", &SteamAPI_ISteamParentalSettings_BIsFeatureInBlockList, py::arg("eFeature"))
 	;
 	m.def("SteamRemotePlay",[]() -> ISteamRemotePlay* { return SteamRemotePlay(); }, py::return_value_policy::reference);
 	py::class_<ISteamRemotePlay, std::unique_ptr<ISteamRemotePlay, py::nodelete>> _isteamremoteplay(m, "ISteamRemotePlay");
 	_isteamremoteplay
 		.def_property_readonly("ptr", [](ISteamRemotePlay& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def("GetSessionCount", &SteamAPI_ISteamRemotePlay_GetSessionCount)
 		.def("GetSessionID", &SteamAPI_ISteamRemotePlay_GetSessionID, py::arg("iSessionIndex"))
 		.def("BSessionRemotePlayTogether", &SteamAPI_ISteamRemotePlay_BSessionRemotePlayTogether, py::arg("unSessionID"))
 		.def("GetSessionSteamID", &SteamAPI_ISteamRemotePlay_GetSessionSteamID, py::arg("unSessionID"))
 		.def("GetSessionGuestID", &SteamAPI_ISteamRemotePlay_GetSessionGuestID, py::arg("unSessionID"))
 		.def("GetSmallSessionAvatar", &SteamAPI_ISteamRemotePlay_GetSmallSessionAvatar, py::arg("unSessionID"))
 		.def("GetMediumSessionAvatar", &SteamAPI_ISteamRemotePlay_GetMediumSessionAvatar, py::arg("unSessionID"))
 		.def("GetLargeSessionAvatar", &SteamAPI_ISteamRemotePlay_GetLargeSessionAvatar, py::arg("unSessionID"))
 		.def("GetSessionClientName", &SteamAPI_ISteamRemotePlay_GetSessionClientName, py::arg("unSessionID"))
 		.def("GetSessionClientFormFactor", &SteamAPI_ISteamRemotePlay_GetSessionClientFormFactor, py::arg("unSessionID"))
 		.def("BGetSessionClientResolution", [](ISteamRemotePlay* self, RemotePlaySessionID_t unSessionID, uintptr_t pnResolutionX, uintptr_t pnResolutionY) { 
			int * ptr1 = reinterpret_cast<int *>(pnResolutionX);
			int * ptr2 = reinterpret_cast<int *>(pnResolutionY);
			return (bool)SteamAPI_ISteamRemotePlay_BGetSessionClientResolution(self, unSessionID, ptr1, ptr2); }, py::arg("unSessionID"), py::arg("pnResolutionX"), py::arg("pnResolutionY"))
 		.def("ShowRemotePlayTogetherUI", &SteamAPI_ISteamRemotePlay_ShowRemotePlayTogetherUI)
 		.def("BSendRemotePlayTogetherInvite", &SteamAPI_ISteamRemotePlay_BSendRemotePlayTogetherInvite, py::arg("steamIDFriend"))
 		.def("BEnableRemotePlayTogetherDirectInput", &SteamAPI_ISteamRemotePlay_BEnableRemotePlayTogetherDirectInput)
 		.def("DisableRemotePlayTogetherDirectInput", &SteamAPI_ISteamRemotePlay_DisableRemotePlayTogetherDirectInput)
 		.def("GetInput", [](ISteamRemotePlay* self, uintptr_t pInput, uint32 unMaxEvents) { 
			RemotePlayInput_t * ptr0 = reinterpret_cast<RemotePlayInput_t *>(pInput);
			return (uint32)SteamAPI_ISteamRemotePlay_GetInput(self, ptr0, unMaxEvents); }, py::arg("pInput"), py::arg("unMaxEvents"))
 		.def("SetMouseVisibility", &SteamAPI_ISteamRemotePlay_SetMouseVisibility, py::arg("unSessionID"), py::arg("bVisible"))
 		.def("SetMousePosition", &SteamAPI_ISteamRemotePlay_SetMousePosition, py::arg("unSessionID"), py::arg("flNormalizedX"), py::arg("flNormalizedY"))
 		.def("CreateMouseCursor", [](ISteamRemotePlay* self, int nWidth, int nHeight, int nHotX, int nHotY, uintptr_t pBGRA, int nPitch) { 
			const void * ptr4 = reinterpret_cast<const void *>(pBGRA);
			return (RemotePlayCursorID_t)SteamAPI_ISteamRemotePlay_CreateMouseCursor(self, nWidth, nHeight, nHotX, nHotY, ptr4, nPitch); }, py::arg("nWidth"), py::arg("nHeight"), py::arg("nHotX"), py::arg("nHotY"), py::arg("pBGRA"), py::arg("nPitch"))
 		.def("SetMouseCursor", &SteamAPI_ISteamRemotePlay_SetMouseCursor, py::arg("unSessionID"), py::arg("unCursorID"))
 	;
 	m.def("SteamNetworkingMessages",[]() -> ISteamNetworkingMessages* { return SteamNetworkingMessages(); }, py::return_value_policy::reference);
 	py::class_<ISteamNetworkingMessages, std::unique_ptr<ISteamNetworkingMessages, py::nodelete>> _isteamnetworkingmessages(m, "ISteamNetworkingMessages");
 	_isteamnetworkingmessages
 		.def_property_readonly("ptr", [](ISteamNetworkingMessages& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def("SendMessageToUser", [](ISteamNetworkingMessages* self, const SteamNetworkingIdentity & identityRemote, uintptr_t pubData, uint32 cubData, int nSendFlags, int nRemoteChannel) { 
			const void * ptr1 = reinterpret_cast<const void *>(pubData);
			return (EResult)SteamAPI_ISteamNetworkingMessages_SendMessageToUser(self, identityRemote, ptr1, cubData, nSendFlags, nRemoteChannel); }, py::arg("identityRemote"), py::arg("pubData"), py::arg("cubData"), py::arg("nSendFlags"), py::arg("nRemoteChannel"))
 		.def("ReceiveMessagesOnChannel", [](ISteamNetworkingMessages* self, int nLocalChannel, uintptr_t ppOutMessages, int nMaxMessages) { 
			SteamNetworkingMessage_t ** ptr1 = reinterpret_cast<SteamNetworkingMessage_t **>(ppOutMessages);
			return (int)SteamAPI_ISteamNetworkingMessages_ReceiveMessagesOnChannel(self, nLocalChannel, ptr1, nMaxMessages); }, py::arg("nLocalChannel"), py::arg("ppOutMessages"), py::arg("nMaxMessages"))
 		.def("AcceptSessionWithUser", &SteamAPI_ISteamNetworkingMessages_AcceptSessionWithUser, py::arg("identityRemote"))
 		.def("CloseSessionWithUser", &SteamAPI_ISteamNetworkingMessages_CloseSessionWithUser, py::arg("identityRemote"))
 		.def("CloseChannelWithUser", &SteamAPI_ISteamNetworkingMessages_CloseChannelWithUser, py::arg("identityRemote"), py::arg("nLocalChannel"))
 		.def("GetSessionConnectionInfo", [](ISteamNetworkingMessages* self, const SteamNetworkingIdentity & identityRemote, uintptr_t pConnectionInfo, uintptr_t pQuickStatus) { 
			SteamNetConnectionInfo_t * ptr1 = reinterpret_cast<SteamNetConnectionInfo_t *>(pConnectionInfo);
			SteamNetConnectionRealTimeStatus_t * ptr2 = reinterpret_cast<SteamNetConnectionRealTimeStatus_t *>(pQuickStatus);
			return (ESteamNetworkingConnectionState)SteamAPI_ISteamNetworkingMessages_GetSessionConnectionInfo(self, identityRemote, ptr1, ptr2); }, py::arg("identityRemote"), py::arg("pConnectionInfo"), py::arg("pQuickStatus"))
 	;
 	m.def("SteamNetworkingSockets",[]() -> ISteamNetworkingSockets* { return SteamNetworkingSockets(); }, py::return_value_policy::reference);
 	py::class_<ISteamNetworkingSockets, std::unique_ptr<ISteamNetworkingSockets, py::nodelete>> _isteamnetworkingsockets(m, "ISteamNetworkingSockets");
 	_isteamnetworkingsockets
 		.def_property_readonly("ptr", [](ISteamNetworkingSockets& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def("CreateListenSocketIP", [](ISteamNetworkingSockets* self, const SteamNetworkingIPAddr & localAddress, int nOptions, uintptr_t pOptions) { 
			const SteamNetworkingConfigValue_t * ptr2 = reinterpret_cast<const SteamNetworkingConfigValue_t *>(pOptions);
			return (HSteamListenSocket)SteamAPI_ISteamNetworkingSockets_CreateListenSocketIP(self, localAddress, nOptions, ptr2); }, py::arg("localAddress"), py::arg("nOptions"), py::arg("pOptions"))
 		.def("ConnectByIPAddress", [](ISteamNetworkingSockets* self, const SteamNetworkingIPAddr & address, int nOptions, uintptr_t pOptions) { 
			const SteamNetworkingConfigValue_t * ptr2 = reinterpret_cast<const SteamNetworkingConfigValue_t *>(pOptions);
			return (HSteamNetConnection)SteamAPI_ISteamNetworkingSockets_ConnectByIPAddress(self, address, nOptions, ptr2); }, py::arg("address"), py::arg("nOptions"), py::arg("pOptions"))
 		.def("CreateListenSocketP2P", [](ISteamNetworkingSockets* self, int nLocalVirtualPort, int nOptions, uintptr_t pOptions) { 
			const SteamNetworkingConfigValue_t * ptr2 = reinterpret_cast<const SteamNetworkingConfigValue_t *>(pOptions);
			return (HSteamListenSocket)SteamAPI_ISteamNetworkingSockets_CreateListenSocketP2P(self, nLocalVirtualPort, nOptions, ptr2); }, py::arg("nLocalVirtualPort"), py::arg("nOptions"), py::arg("pOptions"))
 		.def("ConnectP2P", [](ISteamNetworkingSockets* self, const SteamNetworkingIdentity & identityRemote, int nRemoteVirtualPort, int nOptions, uintptr_t pOptions) { 
			const SteamNetworkingConfigValue_t * ptr3 = reinterpret_cast<const SteamNetworkingConfigValue_t *>(pOptions);
			return (HSteamNetConnection)SteamAPI_ISteamNetworkingSockets_ConnectP2P(self, identityRemote, nRemoteVirtualPort, nOptions, ptr3); }, py::arg("identityRemote"), py::arg("nRemoteVirtualPort"), py::arg("nOptions"), py::arg("pOptions"))
 		.def("AcceptConnection", &SteamAPI_ISteamNetworkingSockets_AcceptConnection, py::arg("hConn"))
 		.def("CloseConnection", [](ISteamNetworkingSockets* self, HSteamNetConnection hPeer, int nReason, uintptr_t pszDebug, bool bEnableLinger) { 
			const char * ptr2 = reinterpret_cast<const char *>(pszDebug);
			return (bool)SteamAPI_ISteamNetworkingSockets_CloseConnection(self, hPeer, nReason, ptr2, bEnableLinger); }, py::arg("hPeer"), py::arg("nReason"), py::arg("pszDebug"), py::arg("bEnableLinger"))
 		.def("CloseListenSocket", &SteamAPI_ISteamNetworkingSockets_CloseListenSocket, py::arg("hSocket"))
 		.def("SetConnectionUserData", &SteamAPI_ISteamNetworkingSockets_SetConnectionUserData, py::arg("hPeer"), py::arg("nUserData"))
 		.def("GetConnectionUserData", &SteamAPI_ISteamNetworkingSockets_GetConnectionUserData, py::arg("hPeer"))
 		.def("SetConnectionName", [](ISteamNetworkingSockets* self, HSteamNetConnection hPeer, uintptr_t pszName) { 
			const char * ptr1 = reinterpret_cast<const char *>(pszName);
			SteamAPI_ISteamNetworkingSockets_SetConnectionName(self, hPeer, ptr1); }, py::arg("hPeer"), py::arg("pszName"))
 		.def("GetConnectionName", [](ISteamNetworkingSockets* self, HSteamNetConnection hPeer, uintptr_t pszName, int nMaxLen) { 
			char * ptr1 = reinterpret_cast<char *>(pszName);
			return (bool)SteamAPI_ISteamNetworkingSockets_GetConnectionName(self, hPeer, ptr1, nMaxLen); }, py::arg("hPeer"), py::arg("pszName"), py::arg("nMaxLen"))
 		.def("SendMessageToConnection", [](ISteamNetworkingSockets* self, HSteamNetConnection hConn, uintptr_t pData, uint32 cbData, int nSendFlags, uintptr_t pOutMessageNumber) { 
			const void * ptr1 = reinterpret_cast<const void *>(pData);
			int64 * ptr4 = reinterpret_cast<int64 *>(pOutMessageNumber);
			return (EResult)SteamAPI_ISteamNetworkingSockets_SendMessageToConnection(self, hConn, ptr1, cbData, nSendFlags, ptr4); }, py::arg("hConn"), py::arg("pData"), py::arg("cbData"), py::arg("nSendFlags"), py::arg("pOutMessageNumber"))
 		.def("SendMessages", [](ISteamNetworkingSockets* self, int nMessages, uintptr_t pMessages, uintptr_t pOutMessageNumberOrResult) { 
			SteamNetworkingMessage_t *const * ptr1 = reinterpret_cast<SteamNetworkingMessage_t *const *>(pMessages);
			int64 * ptr2 = reinterpret_cast<int64 *>(pOutMessageNumberOrResult);
			SteamAPI_ISteamNetworkingSockets_SendMessages(self, nMessages, ptr1, ptr2); }, py::arg("nMessages"), py::arg("pMessages"), py::arg("pOutMessageNumberOrResult"))
 		.def("FlushMessagesOnConnection", &SteamAPI_ISteamNetworkingSockets_FlushMessagesOnConnection, py::arg("hConn"))
 		.def("ReceiveMessagesOnConnection", [](ISteamNetworkingSockets* self, HSteamNetConnection hConn, uintptr_t ppOutMessages, int nMaxMessages) { 
			SteamNetworkingMessage_t ** ptr1 = reinterpret_cast<SteamNetworkingMessage_t **>(ppOutMessages);
			return (int)SteamAPI_ISteamNetworkingSockets_ReceiveMessagesOnConnection(self, hConn, ptr1, nMaxMessages); }, py::arg("hConn"), py::arg("ppOutMessages"), py::arg("nMaxMessages"))
 		.def("GetConnectionInfo", [](ISteamNetworkingSockets* self, HSteamNetConnection hConn, uintptr_t pInfo) { 
			SteamNetConnectionInfo_t * ptr1 = reinterpret_cast<SteamNetConnectionInfo_t *>(pInfo);
			return (bool)SteamAPI_ISteamNetworkingSockets_GetConnectionInfo(self, hConn, ptr1); }, py::arg("hConn"), py::arg("pInfo"))
 		.def("GetConnectionRealTimeStatus", [](ISteamNetworkingSockets* self, HSteamNetConnection hConn, uintptr_t pStatus, int nLanes, uintptr_t pLanes) { 
			SteamNetConnectionRealTimeStatus_t * ptr1 = reinterpret_cast<SteamNetConnectionRealTimeStatus_t *>(pStatus);
			SteamNetConnectionRealTimeLaneStatus_t * ptr3 = reinterpret_cast<SteamNetConnectionRealTimeLaneStatus_t *>(pLanes);
			return (EResult)SteamAPI_ISteamNetworkingSockets_GetConnectionRealTimeStatus(self, hConn, ptr1, nLanes, ptr3); }, py::arg("hConn"), py::arg("pStatus"), py::arg("nLanes"), py::arg("pLanes"))
 		.def("GetDetailedConnectionStatus", [](ISteamNetworkingSockets* self, HSteamNetConnection hConn, uintptr_t pszBuf, int cbBuf) { 
			char * ptr1 = reinterpret_cast<char *>(pszBuf);
			return (int)SteamAPI_ISteamNetworkingSockets_GetDetailedConnectionStatus(self, hConn, ptr1, cbBuf); }, py::arg("hConn"), py::arg("pszBuf"), py::arg("cbBuf"))
 		.def("GetListenSocketAddress", [](ISteamNetworkingSockets* self, HSteamListenSocket hSocket, uintptr_t address) { 
			SteamNetworkingIPAddr * ptr1 = reinterpret_cast<SteamNetworkingIPAddr *>(address);
			return (bool)SteamAPI_ISteamNetworkingSockets_GetListenSocketAddress(self, hSocket, ptr1); }, py::arg("hSocket"), py::arg("address"))
 		.def("CreateSocketPair", [](ISteamNetworkingSockets* self, uintptr_t pOutConnection1, uintptr_t pOutConnection2, bool bUseNetworkLoopback, uintptr_t pIdentity1, uintptr_t pIdentity2) { 
			HSteamNetConnection * ptr0 = reinterpret_cast<HSteamNetConnection *>(pOutConnection1);
			HSteamNetConnection * ptr1 = reinterpret_cast<HSteamNetConnection *>(pOutConnection2);
			const SteamNetworkingIdentity * ptr3 = reinterpret_cast<const SteamNetworkingIdentity *>(pIdentity1);
			const SteamNetworkingIdentity * ptr4 = reinterpret_cast<const SteamNetworkingIdentity *>(pIdentity2);
			return (bool)SteamAPI_ISteamNetworkingSockets_CreateSocketPair(self, ptr0, ptr1, bUseNetworkLoopback, ptr3, ptr4); }, py::arg("pOutConnection1"), py::arg("pOutConnection2"), py::arg("bUseNetworkLoopback"), py::arg("pIdentity1"), py::arg("pIdentity2"))
 		.def("ConfigureConnectionLanes", [](ISteamNetworkingSockets* self, HSteamNetConnection hConn, int nNumLanes, uintptr_t pLanePriorities, uintptr_t pLaneWeights) { 
			const int * ptr2 = reinterpret_cast<const int *>(pLanePriorities);
			const uint16 * ptr3 = reinterpret_cast<const uint16 *>(pLaneWeights);
			return (EResult)SteamAPI_ISteamNetworkingSockets_ConfigureConnectionLanes(self, hConn, nNumLanes, ptr2, ptr3); }, py::arg("hConn"), py::arg("nNumLanes"), py::arg("pLanePriorities"), py::arg("pLaneWeights"))
 		.def("GetIdentity", [](ISteamNetworkingSockets* self, uintptr_t pIdentity) { 
			SteamNetworkingIdentity * ptr0 = reinterpret_cast<SteamNetworkingIdentity *>(pIdentity);
			return (bool)SteamAPI_ISteamNetworkingSockets_GetIdentity(self, ptr0); }, py::arg("pIdentity"))
 		.def("InitAuthentication", &SteamAPI_ISteamNetworkingSockets_InitAuthentication)
 		.def("GetAuthenticationStatus", [](ISteamNetworkingSockets* self, uintptr_t pDetails) { 
			SteamNetAuthenticationStatus_t * ptr0 = reinterpret_cast<SteamNetAuthenticationStatus_t *>(pDetails);
			return (ESteamNetworkingAvailability)SteamAPI_ISteamNetworkingSockets_GetAuthenticationStatus(self, ptr0); }, py::arg("pDetails"))
 		.def("CreatePollGroup", &SteamAPI_ISteamNetworkingSockets_CreatePollGroup)
 		.def("DestroyPollGroup", &SteamAPI_ISteamNetworkingSockets_DestroyPollGroup, py::arg("hPollGroup"))
 		.def("SetConnectionPollGroup", &SteamAPI_ISteamNetworkingSockets_SetConnectionPollGroup, py::arg("hConn"), py::arg("hPollGroup"))
 		.def("ReceiveMessagesOnPollGroup", [](ISteamNetworkingSockets* self, HSteamNetPollGroup hPollGroup, uintptr_t ppOutMessages, int nMaxMessages) { 
			SteamNetworkingMessage_t ** ptr1 = reinterpret_cast<SteamNetworkingMessage_t **>(ppOutMessages);
			return (int)SteamAPI_ISteamNetworkingSockets_ReceiveMessagesOnPollGroup(self, hPollGroup, ptr1, nMaxMessages); }, py::arg("hPollGroup"), py::arg("ppOutMessages"), py::arg("nMaxMessages"))
 		.def("ReceivedRelayAuthTicket", [](ISteamNetworkingSockets* self, uintptr_t pvTicket, int cbTicket, uintptr_t pOutParsedTicket) { 
			const void * ptr0 = reinterpret_cast<const void *>(pvTicket);
			SteamDatagramRelayAuthTicket * ptr2 = reinterpret_cast<SteamDatagramRelayAuthTicket *>(pOutParsedTicket);
			return (bool)SteamAPI_ISteamNetworkingSockets_ReceivedRelayAuthTicket(self, ptr0, cbTicket, ptr2); }, py::arg("pvTicket"), py::arg("cbTicket"), py::arg("pOutParsedTicket"))
 		.def("FindRelayAuthTicketForServer", [](ISteamNetworkingSockets* self, const SteamNetworkingIdentity & identityGameServer, int nRemoteVirtualPort, uintptr_t pOutParsedTicket) { 
			SteamDatagramRelayAuthTicket * ptr2 = reinterpret_cast<SteamDatagramRelayAuthTicket *>(pOutParsedTicket);
			return (int)SteamAPI_ISteamNetworkingSockets_FindRelayAuthTicketForServer(self, identityGameServer, nRemoteVirtualPort, ptr2); }, py::arg("identityGameServer"), py::arg("nRemoteVirtualPort"), py::arg("pOutParsedTicket"))
 		.def("ConnectToHostedDedicatedServer", [](ISteamNetworkingSockets* self, const SteamNetworkingIdentity & identityTarget, int nRemoteVirtualPort, int nOptions, uintptr_t pOptions) { 
			const SteamNetworkingConfigValue_t * ptr3 = reinterpret_cast<const SteamNetworkingConfigValue_t *>(pOptions);
			return (HSteamNetConnection)SteamAPI_ISteamNetworkingSockets_ConnectToHostedDedicatedServer(self, identityTarget, nRemoteVirtualPort, nOptions, ptr3); }, py::arg("identityTarget"), py::arg("nRemoteVirtualPort"), py::arg("nOptions"), py::arg("pOptions"))
 		.def("GetHostedDedicatedServerPort", &SteamAPI_ISteamNetworkingSockets_GetHostedDedicatedServerPort)
 		.def("GetHostedDedicatedServerPOPID", &SteamAPI_ISteamNetworkingSockets_GetHostedDedicatedServerPOPID)
 		.def("GetHostedDedicatedServerAddress", [](ISteamNetworkingSockets* self, uintptr_t pRouting) { 
			SteamDatagramHostedAddress * ptr0 = reinterpret_cast<SteamDatagramHostedAddress *>(pRouting);
			return (EResult)SteamAPI_ISteamNetworkingSockets_GetHostedDedicatedServerAddress(self, ptr0); }, py::arg("pRouting"))
 		.def("CreateHostedDedicatedServerListenSocket", [](ISteamNetworkingSockets* self, int nLocalVirtualPort, int nOptions, uintptr_t pOptions) { 
			const SteamNetworkingConfigValue_t * ptr2 = reinterpret_cast<const SteamNetworkingConfigValue_t *>(pOptions);
			return (HSteamListenSocket)SteamAPI_ISteamNetworkingSockets_CreateHostedDedicatedServerListenSocket(self, nLocalVirtualPort, nOptions, ptr2); }, py::arg("nLocalVirtualPort"), py::arg("nOptions"), py::arg("pOptions"))
 		.def("GetGameCoordinatorServerLogin", [](ISteamNetworkingSockets* self, uintptr_t pLoginInfo, uintptr_t pcbSignedBlob, uintptr_t pBlob) { 
			SteamDatagramGameCoordinatorServerLogin * ptr0 = reinterpret_cast<SteamDatagramGameCoordinatorServerLogin *>(pLoginInfo);
			int * ptr1 = reinterpret_cast<int *>(pcbSignedBlob);
			void * ptr2 = reinterpret_cast<void *>(pBlob);
			return (EResult)SteamAPI_ISteamNetworkingSockets_GetGameCoordinatorServerLogin(self, ptr0, ptr1, ptr2); }, py::arg("pLoginInfo"), py::arg("pcbSignedBlob"), py::arg("pBlob"))
 		.def("ConnectP2PCustomSignaling", [](ISteamNetworkingSockets* self, uintptr_t pSignaling, uintptr_t pPeerIdentity, int nRemoteVirtualPort, int nOptions, uintptr_t pOptions) { 
			ISteamNetworkingConnectionSignaling * ptr0 = reinterpret_cast<ISteamNetworkingConnectionSignaling *>(pSignaling);
			const SteamNetworkingIdentity * ptr1 = reinterpret_cast<const SteamNetworkingIdentity *>(pPeerIdentity);
			const SteamNetworkingConfigValue_t * ptr4 = reinterpret_cast<const SteamNetworkingConfigValue_t *>(pOptions);
			return (HSteamNetConnection)SteamAPI_ISteamNetworkingSockets_ConnectP2PCustomSignaling(self, ptr0, ptr1, nRemoteVirtualPort, nOptions, ptr4); }, py::arg("pSignaling"), py::arg("pPeerIdentity"), py::arg("nRemoteVirtualPort"), py::arg("nOptions"), py::arg("pOptions"))
 		.def("ReceivedP2PCustomSignal", [](ISteamNetworkingSockets* self, uintptr_t pMsg, int cbMsg, uintptr_t pContext) { 
			const void * ptr0 = reinterpret_cast<const void *>(pMsg);
			ISteamNetworkingSignalingRecvContext * ptr2 = reinterpret_cast<ISteamNetworkingSignalingRecvContext *>(pContext);
			return (bool)SteamAPI_ISteamNetworkingSockets_ReceivedP2PCustomSignal(self, ptr0, cbMsg, ptr2); }, py::arg("pMsg"), py::arg("cbMsg"), py::arg("pContext"))
 		.def("GetCertificateRequest", [](ISteamNetworkingSockets* self, uintptr_t pcbBlob, uintptr_t pBlob, uintptr_t errMsg) { 
			int * ptr0 = reinterpret_cast<int *>(pcbBlob);
			void * ptr1 = reinterpret_cast<void *>(pBlob);
			SteamNetworkingErrMsg & ptr2 = *reinterpret_cast<SteamNetworkingErrMsg *>(errMsg);
			return (bool)SteamAPI_ISteamNetworkingSockets_GetCertificateRequest(self, ptr0, ptr1, ptr2); }, py::arg("pcbBlob"), py::arg("pBlob"), py::arg("errMsg"))
 		.def("SetCertificate", [](ISteamNetworkingSockets* self, uintptr_t pCertificate, int cbCertificate, uintptr_t errMsg) { 
			const void * ptr0 = reinterpret_cast<const void *>(pCertificate);
			SteamNetworkingErrMsg & ptr2 = *reinterpret_cast<SteamNetworkingErrMsg *>(errMsg);
			return (bool)SteamAPI_ISteamNetworkingSockets_SetCertificate(self, ptr0, cbCertificate, ptr2); }, py::arg("pCertificate"), py::arg("cbCertificate"), py::arg("errMsg"))
 		.def("ResetIdentity", [](ISteamNetworkingSockets* self, uintptr_t pIdentity) { 
			const SteamNetworkingIdentity * ptr0 = reinterpret_cast<const SteamNetworkingIdentity *>(pIdentity);
			SteamAPI_ISteamNetworkingSockets_ResetIdentity(self, ptr0); }, py::arg("pIdentity"))
 		.def("RunCallbacks", &SteamAPI_ISteamNetworkingSockets_RunCallbacks)
 		.def("BeginAsyncRequestFakeIP", &SteamAPI_ISteamNetworkingSockets_BeginAsyncRequestFakeIP, py::arg("nNumPorts"))
 		.def("GetFakeIP", [](ISteamNetworkingSockets* self, int idxFirstPort, uintptr_t pInfo) { 
			SteamNetworkingFakeIPResult_t * ptr1 = reinterpret_cast<SteamNetworkingFakeIPResult_t *>(pInfo);
			SteamAPI_ISteamNetworkingSockets_GetFakeIP(self, idxFirstPort, ptr1); }, py::arg("idxFirstPort"), py::arg("pInfo"))
 		.def("CreateListenSocketP2PFakeIP", [](ISteamNetworkingSockets* self, int idxFakePort, int nOptions, uintptr_t pOptions) { 
			const SteamNetworkingConfigValue_t * ptr2 = reinterpret_cast<const SteamNetworkingConfigValue_t *>(pOptions);
			return (HSteamListenSocket)SteamAPI_ISteamNetworkingSockets_CreateListenSocketP2PFakeIP(self, idxFakePort, nOptions, ptr2); }, py::arg("idxFakePort"), py::arg("nOptions"), py::arg("pOptions"))
 		.def("GetRemoteFakeIPForConnection", [](ISteamNetworkingSockets* self, HSteamNetConnection hConn, uintptr_t pOutAddr) { 
			SteamNetworkingIPAddr * ptr1 = reinterpret_cast<SteamNetworkingIPAddr *>(pOutAddr);
			return (EResult)SteamAPI_ISteamNetworkingSockets_GetRemoteFakeIPForConnection(self, hConn, ptr1); }, py::arg("hConn"), py::arg("pOutAddr"))
 		.def("CreateFakeUDPPort", &SteamAPI_ISteamNetworkingSockets_CreateFakeUDPPort, py::arg("idxFakeServerPort"))
 	;
 	m.def("SteamNetworkingUtils",[]() -> ISteamNetworkingUtils* { return SteamNetworkingUtils(); }, py::return_value_policy::reference);
 	py::class_<ISteamNetworkingUtils, std::unique_ptr<ISteamNetworkingUtils, py::nodelete>> _isteamnetworkingutils(m, "ISteamNetworkingUtils");
 	_isteamnetworkingutils
 		.def_property_readonly("ptr", [](ISteamNetworkingUtils& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def("AllocateMessage", &SteamAPI_ISteamNetworkingUtils_AllocateMessage, py::arg("cbAllocateBuffer"))
 		.def("InitRelayNetworkAccess", &SteamAPI_ISteamNetworkingUtils_InitRelayNetworkAccess)
 		.def("GetRelayNetworkStatus", [](ISteamNetworkingUtils* self, uintptr_t pDetails) { 
			SteamRelayNetworkStatus_t * ptr0 = reinterpret_cast<SteamRelayNetworkStatus_t *>(pDetails);
			return (ESteamNetworkingAvailability)SteamAPI_ISteamNetworkingUtils_GetRelayNetworkStatus(self, ptr0); }, py::arg("pDetails"))
 		.def("GetLocalPingLocation", [](ISteamNetworkingUtils* self, uintptr_t result) { 
			SteamNetworkPingLocation_t & ptr0 = *reinterpret_cast<SteamNetworkPingLocation_t *>(result);
			return (float)SteamAPI_ISteamNetworkingUtils_GetLocalPingLocation(self, ptr0); }, py::arg("result"))
 		.def("EstimatePingTimeBetweenTwoLocations", &SteamAPI_ISteamNetworkingUtils_EstimatePingTimeBetweenTwoLocations, py::arg("location1"), py::arg("location2"))
 		.def("EstimatePingTimeFromLocalHost", &SteamAPI_ISteamNetworkingUtils_EstimatePingTimeFromLocalHost, py::arg("remoteLocation"))
 		.def("ConvertPingLocationToString", [](ISteamNetworkingUtils* self, const SteamNetworkPingLocation_t & location, uintptr_t pszBuf, int cchBufSize) { 
			char * ptr1 = reinterpret_cast<char *>(pszBuf);
			SteamAPI_ISteamNetworkingUtils_ConvertPingLocationToString(self, location, ptr1, cchBufSize); }, py::arg("location"), py::arg("pszBuf"), py::arg("cchBufSize"))
 		.def("ParsePingLocationString", [](ISteamNetworkingUtils* self, uintptr_t pszString, uintptr_t result) { 
			const char * ptr0 = reinterpret_cast<const char *>(pszString);
			SteamNetworkPingLocation_t & ptr1 = *reinterpret_cast<SteamNetworkPingLocation_t *>(result);
			return (bool)SteamAPI_ISteamNetworkingUtils_ParsePingLocationString(self, ptr0, ptr1); }, py::arg("pszString"), py::arg("result"))
 		.def("CheckPingDataUpToDate", &SteamAPI_ISteamNetworkingUtils_CheckPingDataUpToDate, py::arg("flMaxAgeSeconds"))
 		.def("GetPingToDataCenter", [](ISteamNetworkingUtils* self, SteamNetworkingPOPID popID, uintptr_t pViaRelayPoP) { 
			SteamNetworkingPOPID * ptr1 = reinterpret_cast<SteamNetworkingPOPID *>(pViaRelayPoP);
			return (int)SteamAPI_ISteamNetworkingUtils_GetPingToDataCenter(self, popID, ptr1); }, py::arg("popID"), py::arg("pViaRelayPoP"))
 		.def("GetDirectPingToPOP", &SteamAPI_ISteamNetworkingUtils_GetDirectPingToPOP, py::arg("popID"))
 		.def("GetPOPCount", &SteamAPI_ISteamNetworkingUtils_GetPOPCount)
 		.def("GetPOPList", [](ISteamNetworkingUtils* self, uintptr_t list, int nListSz) { 
			SteamNetworkingPOPID * ptr0 = reinterpret_cast<SteamNetworkingPOPID *>(list);
			return (int)SteamAPI_ISteamNetworkingUtils_GetPOPList(self, ptr0, nListSz); }, py::arg("list"), py::arg("nListSz"))
 		.def("GetLocalTimestamp", &SteamAPI_ISteamNetworkingUtils_GetLocalTimestamp)
 		.def("SetDebugOutputFunction", &SteamAPI_ISteamNetworkingUtils_SetDebugOutputFunction, py::arg("eDetailLevel"), py::arg("pfnFunc"))
 		.def("IsFakeIPv4", &SteamAPI_ISteamNetworkingUtils_IsFakeIPv4, py::arg("nIPv4"))
 		.def("GetIPv4FakeIPType", &SteamAPI_ISteamNetworkingUtils_GetIPv4FakeIPType, py::arg("nIPv4"))
 		.def("GetRealIdentityForFakeIP", [](ISteamNetworkingUtils* self, const SteamNetworkingIPAddr & fakeIP, uintptr_t pOutRealIdentity) { 
			SteamNetworkingIdentity * ptr1 = reinterpret_cast<SteamNetworkingIdentity *>(pOutRealIdentity);
			return (EResult)SteamAPI_ISteamNetworkingUtils_GetRealIdentityForFakeIP(self, fakeIP, ptr1); }, py::arg("fakeIP"), py::arg("pOutRealIdentity"))
 		.def("SetGlobalConfigValueInt32", &SteamAPI_ISteamNetworkingUtils_SetGlobalConfigValueInt32, py::arg("eValue"), py::arg("val"))
 		.def("SetGlobalConfigValueFloat", &SteamAPI_ISteamNetworkingUtils_SetGlobalConfigValueFloat, py::arg("eValue"), py::arg("val"))
 		.def("SetGlobalConfigValueString", [](ISteamNetworkingUtils* self, ESteamNetworkingConfigValue eValue, uintptr_t val) { 
			const char * ptr1 = reinterpret_cast<const char *>(val);
			return (bool)SteamAPI_ISteamNetworkingUtils_SetGlobalConfigValueString(self, eValue, ptr1); }, py::arg("eValue"), py::arg("val"))
 		.def("SetGlobalConfigValuePtr", [](ISteamNetworkingUtils* self, ESteamNetworkingConfigValue eValue, uintptr_t val) { 
			void * ptr1 = reinterpret_cast<void *>(val);
			return (bool)SteamAPI_ISteamNetworkingUtils_SetGlobalConfigValuePtr(self, eValue, ptr1); }, py::arg("eValue"), py::arg("val"))
 		.def("SetConnectionConfigValueInt32", &SteamAPI_ISteamNetworkingUtils_SetConnectionConfigValueInt32, py::arg("hConn"), py::arg("eValue"), py::arg("val"))
 		.def("SetConnectionConfigValueFloat", &SteamAPI_ISteamNetworkingUtils_SetConnectionConfigValueFloat, py::arg("hConn"), py::arg("eValue"), py::arg("val"))
 		.def("SetConnectionConfigValueString", [](ISteamNetworkingUtils* self, HSteamNetConnection hConn, ESteamNetworkingConfigValue eValue, uintptr_t val) { 
			const char * ptr2 = reinterpret_cast<const char *>(val);
			return (bool)SteamAPI_ISteamNetworkingUtils_SetConnectionConfigValueString(self, hConn, eValue, ptr2); }, py::arg("hConn"), py::arg("eValue"), py::arg("val"))
 		.def("SetGlobalCallback_SteamNetConnectionStatusChanged", &SteamAPI_ISteamNetworkingUtils_SetGlobalCallback_SteamNetConnectionStatusChanged, py::arg("fnCallback"))
 		.def("SetGlobalCallback_SteamNetAuthenticationStatusChanged", &SteamAPI_ISteamNetworkingUtils_SetGlobalCallback_SteamNetAuthenticationStatusChanged, py::arg("fnCallback"))
 		.def("SetGlobalCallback_SteamRelayNetworkStatusChanged", &SteamAPI_ISteamNetworkingUtils_SetGlobalCallback_SteamRelayNetworkStatusChanged, py::arg("fnCallback"))
 		.def("SetGlobalCallback_FakeIPResult", &SteamAPI_ISteamNetworkingUtils_SetGlobalCallback_FakeIPResult, py::arg("fnCallback"))
 		.def("SetGlobalCallback_MessagesSessionRequest", &SteamAPI_ISteamNetworkingUtils_SetGlobalCallback_MessagesSessionRequest, py::arg("fnCallback"))
 		.def("SetGlobalCallback_MessagesSessionFailed", &SteamAPI_ISteamNetworkingUtils_SetGlobalCallback_MessagesSessionFailed, py::arg("fnCallback"))
 		.def("SetConfigValue", [](ISteamNetworkingUtils* self, ESteamNetworkingConfigValue eValue, ESteamNetworkingConfigScope eScopeType, intptr_t scopeObj, ESteamNetworkingConfigDataType eDataType, uintptr_t pArg) { 
			const void * ptr4 = reinterpret_cast<const void *>(pArg);
			return (bool)SteamAPI_ISteamNetworkingUtils_SetConfigValue(self, eValue, eScopeType, scopeObj, eDataType, ptr4); }, py::arg("eValue"), py::arg("eScopeType"), py::arg("scopeObj"), py::arg("eDataType"), py::arg("pArg"))
 		.def("SetConfigValueStruct", &SteamAPI_ISteamNetworkingUtils_SetConfigValueStruct, py::arg("opt"), py::arg("eScopeType"), py::arg("scopeObj"))
 		.def("GetConfigValue", [](ISteamNetworkingUtils* self, ESteamNetworkingConfigValue eValue, ESteamNetworkingConfigScope eScopeType, intptr_t scopeObj, uintptr_t pOutDataType, uintptr_t pResult, uintptr_t cbResult) { 
			ESteamNetworkingConfigDataType * ptr3 = reinterpret_cast<ESteamNetworkingConfigDataType *>(pOutDataType);
			void * ptr4 = reinterpret_cast<void *>(pResult);
			size_t * ptr5 = reinterpret_cast<size_t *>(cbResult);
			return (ESteamNetworkingGetConfigValueResult)SteamAPI_ISteamNetworkingUtils_GetConfigValue(self, eValue, eScopeType, scopeObj, ptr3, ptr4, ptr5); }, py::arg("eValue"), py::arg("eScopeType"), py::arg("scopeObj"), py::arg("pOutDataType"), py::arg("pResult"), py::arg("cbResult"))
 		.def("GetConfigValueInfo", [](ISteamNetworkingUtils* self, ESteamNetworkingConfigValue eValue, uintptr_t pOutDataType, uintptr_t pOutScope) { 
			ESteamNetworkingConfigDataType * ptr1 = reinterpret_cast<ESteamNetworkingConfigDataType *>(pOutDataType);
			ESteamNetworkingConfigScope * ptr2 = reinterpret_cast<ESteamNetworkingConfigScope *>(pOutScope);
			return (const char *)SteamAPI_ISteamNetworkingUtils_GetConfigValueInfo(self, eValue, ptr1, ptr2); }, py::arg("eValue"), py::arg("pOutDataType"), py::arg("pOutScope"))
 		.def("IterateGenericEditableConfigValues", &SteamAPI_ISteamNetworkingUtils_IterateGenericEditableConfigValues, py::arg("eCurrent"), py::arg("bEnumerateDevVars"))
 		.def("SteamNetworkingIPAddr_ToString", [](ISteamNetworkingUtils* self, const SteamNetworkingIPAddr & addr, uintptr_t buf, uint32 cbBuf, bool bWithPort) { 
			char * ptr1 = reinterpret_cast<char *>(buf);
			SteamAPI_ISteamNetworkingUtils_SteamNetworkingIPAddr_ToString(self, addr, ptr1, cbBuf, bWithPort); }, py::arg("addr"), py::arg("buf"), py::arg("cbBuf"), py::arg("bWithPort"))
 		.def("SteamNetworkingIPAddr_ParseString", [](ISteamNetworkingUtils* self, uintptr_t pAddr, uintptr_t pszStr) { 
			SteamNetworkingIPAddr * ptr0 = reinterpret_cast<SteamNetworkingIPAddr *>(pAddr);
			const char * ptr1 = reinterpret_cast<const char *>(pszStr);
			return (bool)SteamAPI_ISteamNetworkingUtils_SteamNetworkingIPAddr_ParseString(self, ptr0, ptr1); }, py::arg("pAddr"), py::arg("pszStr"))
 		.def("SteamNetworkingIPAddr_GetFakeIPType", &SteamAPI_ISteamNetworkingUtils_SteamNetworkingIPAddr_GetFakeIPType, py::arg("addr"))
 		.def("SteamNetworkingIdentity_ToString", [](ISteamNetworkingUtils* self, const SteamNetworkingIdentity & identity, uintptr_t buf, uint32 cbBuf) { 
			char * ptr1 = reinterpret_cast<char *>(buf);
			SteamAPI_ISteamNetworkingUtils_SteamNetworkingIdentity_ToString(self, identity, ptr1, cbBuf); }, py::arg("identity"), py::arg("buf"), py::arg("cbBuf"))
 		.def("SteamNetworkingIdentity_ParseString", [](ISteamNetworkingUtils* self, uintptr_t pIdentity, uintptr_t pszStr) { 
			SteamNetworkingIdentity * ptr0 = reinterpret_cast<SteamNetworkingIdentity *>(pIdentity);
			const char * ptr1 = reinterpret_cast<const char *>(pszStr);
			return (bool)SteamAPI_ISteamNetworkingUtils_SteamNetworkingIdentity_ParseString(self, ptr0, ptr1); }, py::arg("pIdentity"), py::arg("pszStr"))
 	;
 	m.def("SteamGameServer",[]() -> ISteamGameServer* { return SteamGameServer(); }, py::return_value_policy::reference);
 	py::class_<ISteamGameServer, std::unique_ptr<ISteamGameServer, py::nodelete>> _isteamgameserver(m, "ISteamGameServer");
 	_isteamgameserver
 		.def_property_readonly("ptr", [](ISteamGameServer& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def("SetProduct", [](ISteamGameServer* self, uintptr_t pszProduct) { 
			const char * ptr0 = reinterpret_cast<const char *>(pszProduct);
			SteamAPI_ISteamGameServer_SetProduct(self, ptr0); }, py::arg("pszProduct"))
 		.def("SetGameDescription", [](ISteamGameServer* self, uintptr_t pszGameDescription) { 
			const char * ptr0 = reinterpret_cast<const char *>(pszGameDescription);
			SteamAPI_ISteamGameServer_SetGameDescription(self, ptr0); }, py::arg("pszGameDescription"))
 		.def("SetModDir", [](ISteamGameServer* self, uintptr_t pszModDir) { 
			const char * ptr0 = reinterpret_cast<const char *>(pszModDir);
			SteamAPI_ISteamGameServer_SetModDir(self, ptr0); }, py::arg("pszModDir"))
 		.def("SetDedicatedServer", &SteamAPI_ISteamGameServer_SetDedicatedServer, py::arg("bDedicated"))
 		.def("LogOn", [](ISteamGameServer* self, uintptr_t pszToken) { 
			const char * ptr0 = reinterpret_cast<const char *>(pszToken);
			SteamAPI_ISteamGameServer_LogOn(self, ptr0); }, py::arg("pszToken"))
 		.def("LogOnAnonymous", &SteamAPI_ISteamGameServer_LogOnAnonymous)
 		.def("LogOff", &SteamAPI_ISteamGameServer_LogOff)
 		.def("BLoggedOn", &SteamAPI_ISteamGameServer_BLoggedOn)
 		.def("BSecure", &SteamAPI_ISteamGameServer_BSecure)
 		.def("GetSteamID", &SteamAPI_ISteamGameServer_GetSteamID)
 		.def("WasRestartRequested", &SteamAPI_ISteamGameServer_WasRestartRequested)
 		.def("SetMaxPlayerCount", &SteamAPI_ISteamGameServer_SetMaxPlayerCount, py::arg("cPlayersMax"))
 		.def("SetBotPlayerCount", &SteamAPI_ISteamGameServer_SetBotPlayerCount, py::arg("cBotplayers"))
 		.def("SetServerName", [](ISteamGameServer* self, uintptr_t pszServerName) { 
			const char * ptr0 = reinterpret_cast<const char *>(pszServerName);
			SteamAPI_ISteamGameServer_SetServerName(self, ptr0); }, py::arg("pszServerName"))
 		.def("SetMapName", [](ISteamGameServer* self, uintptr_t pszMapName) { 
			const char * ptr0 = reinterpret_cast<const char *>(pszMapName);
			SteamAPI_ISteamGameServer_SetMapName(self, ptr0); }, py::arg("pszMapName"))
 		.def("SetPasswordProtected", &SteamAPI_ISteamGameServer_SetPasswordProtected, py::arg("bPasswordProtected"))
 		.def("SetSpectatorPort", &SteamAPI_ISteamGameServer_SetSpectatorPort, py::arg("unSpectatorPort"))
 		.def("SetSpectatorServerName", [](ISteamGameServer* self, uintptr_t pszSpectatorServerName) { 
			const char * ptr0 = reinterpret_cast<const char *>(pszSpectatorServerName);
			SteamAPI_ISteamGameServer_SetSpectatorServerName(self, ptr0); }, py::arg("pszSpectatorServerName"))
 		.def("ClearAllKeyValues", &SteamAPI_ISteamGameServer_ClearAllKeyValues)
 		.def("SetKeyValue", [](ISteamGameServer* self, uintptr_t pKey, uintptr_t pValue) { 
			const char * ptr0 = reinterpret_cast<const char *>(pKey);
			const char * ptr1 = reinterpret_cast<const char *>(pValue);
			SteamAPI_ISteamGameServer_SetKeyValue(self, ptr0, ptr1); }, py::arg("pKey"), py::arg("pValue"))
 		.def("SetGameTags", [](ISteamGameServer* self, uintptr_t pchGameTags) { 
			const char * ptr0 = reinterpret_cast<const char *>(pchGameTags);
			SteamAPI_ISteamGameServer_SetGameTags(self, ptr0); }, py::arg("pchGameTags"))
 		.def("SetGameData", [](ISteamGameServer* self, uintptr_t pchGameData) { 
			const char * ptr0 = reinterpret_cast<const char *>(pchGameData);
			SteamAPI_ISteamGameServer_SetGameData(self, ptr0); }, py::arg("pchGameData"))
 		.def("SetRegion", [](ISteamGameServer* self, uintptr_t pszRegion) { 
			const char * ptr0 = reinterpret_cast<const char *>(pszRegion);
			SteamAPI_ISteamGameServer_SetRegion(self, ptr0); }, py::arg("pszRegion"))
 		.def("SetAdvertiseServerActive", &SteamAPI_ISteamGameServer_SetAdvertiseServerActive, py::arg("bActive"))
 		.def("GetAuthSessionTicket", [](ISteamGameServer* self, uintptr_t pTicket, int cbMaxTicket, uintptr_t pcbTicket, uintptr_t pSnid) { 
			void * ptr0 = reinterpret_cast<void *>(pTicket);
			uint32 * ptr2 = reinterpret_cast<uint32 *>(pcbTicket);
			const SteamNetworkingIdentity * ptr3 = reinterpret_cast<const SteamNetworkingIdentity *>(pSnid);
			return (HAuthTicket)SteamAPI_ISteamGameServer_GetAuthSessionTicket(self, ptr0, cbMaxTicket, ptr2, ptr3); }, py::arg("pTicket"), py::arg("cbMaxTicket"), py::arg("pcbTicket"), py::arg("pSnid"))
 		.def("BeginAuthSession", [](ISteamGameServer* self, uintptr_t pAuthTicket, int cbAuthTicket, uint64_steamid steamID) { 
			const void * ptr0 = reinterpret_cast<const void *>(pAuthTicket);
			return (EBeginAuthSessionResult)SteamAPI_ISteamGameServer_BeginAuthSession(self, ptr0, cbAuthTicket, steamID); }, py::arg("pAuthTicket"), py::arg("cbAuthTicket"), py::arg("steamID"))
 		.def("EndAuthSession", &SteamAPI_ISteamGameServer_EndAuthSession, py::arg("steamID"))
 		.def("CancelAuthTicket", &SteamAPI_ISteamGameServer_CancelAuthTicket, py::arg("hAuthTicket"))
 		.def("UserHasLicenseForApp", &SteamAPI_ISteamGameServer_UserHasLicenseForApp, py::arg("steamID"), py::arg("appID"))
 		.def("RequestUserGroupStatus", &SteamAPI_ISteamGameServer_RequestUserGroupStatus, py::arg("steamIDUser"), py::arg("steamIDGroup"))
 		.def("GetGameplayStats", &SteamAPI_ISteamGameServer_GetGameplayStats)
 		.def("GetServerReputation", &SteamAPI_ISteamGameServer_GetServerReputation)
 		.def("GetPublicIP", &SteamAPI_ISteamGameServer_GetPublicIP)
 		.def("HandleIncomingPacket", [](ISteamGameServer* self, uintptr_t pData, int cbData, uint32 srcIP, uint16 srcPort) { 
			const void * ptr0 = reinterpret_cast<const void *>(pData);
			return (bool)SteamAPI_ISteamGameServer_HandleIncomingPacket(self, ptr0, cbData, srcIP, srcPort); }, py::arg("pData"), py::arg("cbData"), py::arg("srcIP"), py::arg("srcPort"))
 		.def("GetNextOutgoingPacket", [](ISteamGameServer* self, uintptr_t pOut, int cbMaxOut, uintptr_t pNetAdr, uintptr_t pPort) { 
			void * ptr0 = reinterpret_cast<void *>(pOut);
			uint32 * ptr2 = reinterpret_cast<uint32 *>(pNetAdr);
			uint16 * ptr3 = reinterpret_cast<uint16 *>(pPort);
			return (int)SteamAPI_ISteamGameServer_GetNextOutgoingPacket(self, ptr0, cbMaxOut, ptr2, ptr3); }, py::arg("pOut"), py::arg("cbMaxOut"), py::arg("pNetAdr"), py::arg("pPort"))
 		.def("AssociateWithClan", &SteamAPI_ISteamGameServer_AssociateWithClan, py::arg("steamIDClan"))
 		.def("ComputeNewPlayerCompatibility", &SteamAPI_ISteamGameServer_ComputeNewPlayerCompatibility, py::arg("steamIDNewPlayer"))
 		.def("CreateUnauthenticatedUserConnection", &SteamAPI_ISteamGameServer_CreateUnauthenticatedUserConnection)
 		.def("BUpdateUserData", [](ISteamGameServer* self, uint64_steamid steamIDUser, uintptr_t pchPlayerName, uint32 uScore) { 
			const char * ptr1 = reinterpret_cast<const char *>(pchPlayerName);
			return (bool)SteamAPI_ISteamGameServer_BUpdateUserData(self, steamIDUser, ptr1, uScore); }, py::arg("steamIDUser"), py::arg("pchPlayerName"), py::arg("uScore"))
 	;
 	m.def("SteamGameServerStats",[]() -> ISteamGameServerStats* { return SteamGameServerStats(); }, py::return_value_policy::reference);
 	py::class_<ISteamGameServerStats, std::unique_ptr<ISteamGameServerStats, py::nodelete>> _isteamgameserverstats(m, "ISteamGameServerStats");
 	_isteamgameserverstats
 		.def_property_readonly("ptr", [](ISteamGameServerStats& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def("RequestUserStats", &SteamAPI_ISteamGameServerStats_RequestUserStats, py::arg("steamIDUser"))
 		.def("GetUserStat", [](ISteamGameServerStats* self, uint64_steamid steamIDUser, uintptr_t pchName, uintptr_t pData) { 
			const char * ptr1 = reinterpret_cast<const char *>(pchName);
			float * ptr2 = reinterpret_cast<float *>(pData);
			return (bool)SteamAPI_ISteamGameServerStats_GetUserStatFloat(self, steamIDUser, ptr1, ptr2); }, py::arg("steamIDUser"), py::arg("pchName"), py::arg("pData"))
 		.def("GetUserAchievement", [](ISteamGameServerStats* self, uint64_steamid steamIDUser, uintptr_t pchName, uintptr_t pbAchieved) { 
			const char * ptr1 = reinterpret_cast<const char *>(pchName);
			bool * ptr2 = reinterpret_cast<bool *>(pbAchieved);
			return (bool)SteamAPI_ISteamGameServerStats_GetUserAchievement(self, steamIDUser, ptr1, ptr2); }, py::arg("steamIDUser"), py::arg("pchName"), py::arg("pbAchieved"))
 		.def("SetUserStat", [](ISteamGameServerStats* self, uint64_steamid steamIDUser, uintptr_t pchName, float fData) { 
			const char * ptr1 = reinterpret_cast<const char *>(pchName);
			return (bool)SteamAPI_ISteamGameServerStats_SetUserStatFloat(self, steamIDUser, ptr1, fData); }, py::arg("steamIDUser"), py::arg("pchName"), py::arg("fData"))
 		.def("UpdateUserAvgRateStat", [](ISteamGameServerStats* self, uint64_steamid steamIDUser, uintptr_t pchName, float flCountThisSession, double dSessionLength) { 
			const char * ptr1 = reinterpret_cast<const char *>(pchName);
			return (bool)SteamAPI_ISteamGameServerStats_UpdateUserAvgRateStat(self, steamIDUser, ptr1, flCountThisSession, dSessionLength); }, py::arg("steamIDUser"), py::arg("pchName"), py::arg("flCountThisSession"), py::arg("dSessionLength"))
 		.def("SetUserAchievement", [](ISteamGameServerStats* self, uint64_steamid steamIDUser, uintptr_t pchName) { 
			const char * ptr1 = reinterpret_cast<const char *>(pchName);
			return (bool)SteamAPI_ISteamGameServerStats_SetUserAchievement(self, steamIDUser, ptr1); }, py::arg("steamIDUser"), py::arg("pchName"))
 		.def("ClearUserAchievement", [](ISteamGameServerStats* self, uint64_steamid steamIDUser, uintptr_t pchName) { 
			const char * ptr1 = reinterpret_cast<const char *>(pchName);
			return (bool)SteamAPI_ISteamGameServerStats_ClearUserAchievement(self, steamIDUser, ptr1); }, py::arg("steamIDUser"), py::arg("pchName"))
 		.def("StoreUserStats", &SteamAPI_ISteamGameServerStats_StoreUserStats, py::arg("steamIDUser"))
 	;
 	py::class_<ISteamNetworkingFakeUDPPort, std::unique_ptr<ISteamNetworkingFakeUDPPort, py::nodelete>> _isteamnetworkingfakeudpport(m, "ISteamNetworkingFakeUDPPort");
 	_isteamnetworkingfakeudpport
 		.def_property_readonly("ptr", [](ISteamNetworkingFakeUDPPort& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def("DestroyFakeUDPPort", &SteamAPI_ISteamNetworkingFakeUDPPort_DestroyFakeUDPPort)
 		.def("SendMessageToFakeIP", [](ISteamNetworkingFakeUDPPort* self, const SteamNetworkingIPAddr & remoteAddress, uintptr_t pData, uint32 cbData, int nSendFlags) { 
			const void * ptr1 = reinterpret_cast<const void *>(pData);
			return (EResult)SteamAPI_ISteamNetworkingFakeUDPPort_SendMessageToFakeIP(self, remoteAddress, ptr1, cbData, nSendFlags); }, py::arg("remoteAddress"), py::arg("pData"), py::arg("cbData"), py::arg("nSendFlags"))
 		.def("ReceiveMessages", [](ISteamNetworkingFakeUDPPort* self, uintptr_t ppOutMessages, int nMaxMessages) { 
			SteamNetworkingMessage_t ** ptr0 = reinterpret_cast<SteamNetworkingMessage_t **>(ppOutMessages);
			return (int)SteamAPI_ISteamNetworkingFakeUDPPort_ReceiveMessages(self, ptr0, nMaxMessages); }, py::arg("ppOutMessages"), py::arg("nMaxMessages"))
 		.def("ScheduleCleanup", &SteamAPI_ISteamNetworkingFakeUDPPort_ScheduleCleanup, py::arg("remoteAddress"))
 	;
 	bind_fixed_array_view<AppId_t, 32>(m, "AppId_tarray32");
 	bind_fixed_array_view<CSteamID, 50>(m, "CSteamIDarray50");
 	bind_fixed_array_view<PublishedFileId_t, 50>(m, "PublishedFileId_tarray50");
 	bind_fixed_array_view<float, 50>(m, "floatarray50");
 	bind_fixed_array_view<uint16, 8>(m, "uint16array8");
 	bind_fixed_array_view<uint32, 10>(m, "uint32array10");
 	bind_fixed_array_view<uint32, 15>(m, "uint32array15");
 	bind_fixed_array_view<uint32, 50>(m, "uint32array50");
 	bind_fixed_array_view<uint32, 63>(m, "uint32array63");
 	bind_fixed_array_view<uint8, 16>(m, "uint8array16");
 	bind_fixed_array_view<uint8, 20>(m, "uint8array20");
 	bind_fixed_array_view<uint8, 2560>(m, "uint8array2560");
 	bind_fixed_array_view<uint8, 512>(m, "uint8array512");
 	bind_response_adapters(m);
 	
}
