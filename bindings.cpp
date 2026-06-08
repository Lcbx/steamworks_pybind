
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <string>
#include <algorithm>

#include "../public/steam/steam_api.h"
#include "../public/steam/steam_api_flat.h"
//#include "../public/steam/steam_gameserver.h" // EServerMode enum
#include "../public/steam/steamnetworkingfakeip.h" // SteamNetworkingFakeIPResult

namespace py = pybind11;


// definitions
// TODO : find some way to detect missing definitions

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


// module

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

 	py::class_<SteamServersConnected_t> _steamserversconnected_t(m, "SteamServersConnected");
 	_steamserversconnected_t
 		.def("_addr", [](SteamServersConnected_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 	;
 	py::class_<SteamServerConnectFailure_t> _steamserverconnectfailure_t(m, "SteamServerConnectFailure");
 	_steamserverconnectfailure_t
 		.def("_addr", [](SteamServerConnectFailure_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("eResult", &SteamServerConnectFailure_t::m_eResult)
 		.def_readwrite("bStillRetrying", &SteamServerConnectFailure_t::m_bStillRetrying)
 	;
 	py::class_<SteamServersDisconnected_t> _steamserversdisconnected_t(m, "SteamServersDisconnected");
 	_steamserversdisconnected_t
 		.def("_addr", [](SteamServersDisconnected_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("eResult", &SteamServersDisconnected_t::m_eResult)
 	;
 	py::class_<ClientGameServerDeny_t> _clientgameserverdeny_t(m, "ClientGameServerDeny");
 	_clientgameserverdeny_t
 		.def("_addr", [](ClientGameServerDeny_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("uAppID", &ClientGameServerDeny_t::m_uAppID)
 		.def_readwrite("unGameServerIP", &ClientGameServerDeny_t::m_unGameServerIP)
 		.def_readwrite("usGameServerPort", &ClientGameServerDeny_t::m_usGameServerPort)
 		.def_readwrite("bSecure", &ClientGameServerDeny_t::m_bSecure)
 		.def_readwrite("uReason", &ClientGameServerDeny_t::m_uReason)
 	;
 	py::class_<IPCFailure_t> _ipcfailure_t(m, "IPCFailure");
 	_ipcfailure_t
 		.def("_addr", [](IPCFailure_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("eFailureType", &IPCFailure_t::m_eFailureType)
 	;
 	py::class_<LicensesUpdated_t> _licensesupdated_t(m, "LicensesUpdated");
 	_licensesupdated_t
 		.def("_addr", [](LicensesUpdated_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 	;
 	py::class_<ValidateAuthTicketResponse_t> _validateauthticketresponse_t(m, "ValidateAuthTicketResponse");
 	_validateauthticketresponse_t
 		.def("_addr", [](ValidateAuthTicketResponse_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("SteamID", &ValidateAuthTicketResponse_t::m_SteamID)
 		.def_readwrite("eAuthSessionResponse", &ValidateAuthTicketResponse_t::m_eAuthSessionResponse)
 		.def_readwrite("OwnerSteamID", &ValidateAuthTicketResponse_t::m_OwnerSteamID)
 	;
 	py::class_<MicroTxnAuthorizationResponse_t> _microtxnauthorizationresponse_t(m, "MicroTxnAuthorizationResponse");
 	_microtxnauthorizationresponse_t
 		.def("_addr", [](MicroTxnAuthorizationResponse_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("unAppID", &MicroTxnAuthorizationResponse_t::m_unAppID)
 		.def_readwrite("ulOrderID", &MicroTxnAuthorizationResponse_t::m_ulOrderID)
 		.def_readwrite("bAuthorized", &MicroTxnAuthorizationResponse_t::m_bAuthorized)
 	;
 	py::class_<EncryptedAppTicketResponse_t> _encryptedappticketresponse_t(m, "EncryptedAppTicketResponse");
 	_encryptedappticketresponse_t
 		.def("_addr", [](EncryptedAppTicketResponse_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("eResult", &EncryptedAppTicketResponse_t::m_eResult)
 	;
 	py::class_<GetAuthSessionTicketResponse_t> _getauthsessionticketresponse_t(m, "GetAuthSessionTicketResponse");
 	_getauthsessionticketresponse_t
 		.def("_addr", [](GetAuthSessionTicketResponse_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("hAuthTicket", &GetAuthSessionTicketResponse_t::m_hAuthTicket)
 		.def_readwrite("eResult", &GetAuthSessionTicketResponse_t::m_eResult)
 	;
 	py::class_<GameWebCallback_t> _gamewebcallback_t(m, "GameWebCallback");
 	_gamewebcallback_t
 		.def("_addr", [](GameWebCallback_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_property("szURL",
			[](const GameWebCallback_t& x) { return get_char_array(x.m_szURL); },
			[](GameWebCallback_t& x, const std::string& s) { set_char_array(x.m_szURL, s); })
 	;
 	py::class_<StoreAuthURLResponse_t> _storeauthurlresponse_t(m, "StoreAuthURLResponse");
 	_storeauthurlresponse_t
 		.def("_addr", [](StoreAuthURLResponse_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_property("szURL",
			[](const StoreAuthURLResponse_t& x) { return get_char_array(x.m_szURL); },
			[](StoreAuthURLResponse_t& x, const std::string& s) { set_char_array(x.m_szURL, s); })
 	;
 	py::class_<MarketEligibilityResponse_t> _marketeligibilityresponse_t(m, "MarketEligibilityResponse");
 	_marketeligibilityresponse_t
 		.def("_addr", [](MarketEligibilityResponse_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("bAllowed", &MarketEligibilityResponse_t::m_bAllowed)
 		.def_readwrite("eNotAllowedReason", &MarketEligibilityResponse_t::m_eNotAllowedReason)
 		.def_readwrite("rtAllowedAtTime", &MarketEligibilityResponse_t::m_rtAllowedAtTime)
 		.def_readwrite("cdaySteamGuardRequiredDays", &MarketEligibilityResponse_t::m_cdaySteamGuardRequiredDays)
 		.def_readwrite("cdayNewDeviceCooldown", &MarketEligibilityResponse_t::m_cdayNewDeviceCooldown)
 	;
 	py::class_<DurationControl_t> _durationcontrol_t(m, "DurationControl");
 	_durationcontrol_t
 		.def("_addr", [](DurationControl_t& self) { return reinterpret_cast<uintptr_t>(&self); })
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
 		.def("_addr", [](GetTicketForWebApiResponse_t& self) { return reinterpret_cast<uintptr_t>(&self); })
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
 		.def("_addr", [](PersonaStateChange_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("ulSteamID", &PersonaStateChange_t::m_ulSteamID)
 		.def_readwrite("nChangeFlags", &PersonaStateChange_t::m_nChangeFlags)
 	;
 	py::class_<GameOverlayActivated_t> _gameoverlayactivated_t(m, "GameOverlayActivated");
 	_gameoverlayactivated_t
 		.def("_addr", [](GameOverlayActivated_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("bActive", &GameOverlayActivated_t::m_bActive)
 		.def_readwrite("bUserInitiated", &GameOverlayActivated_t::m_bUserInitiated)
 		.def_readwrite("nAppID", &GameOverlayActivated_t::m_nAppID)
 		.def_readwrite("dwOverlayPID", &GameOverlayActivated_t::m_dwOverlayPID)
 	;
 	py::class_<GameServerChangeRequested_t> _gameserverchangerequested_t(m, "GameServerChangeRequested");
 	_gameserverchangerequested_t
 		.def("_addr", [](GameServerChangeRequested_t& self) { return reinterpret_cast<uintptr_t>(&self); })
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
 		.def("_addr", [](GameLobbyJoinRequested_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("steamIDLobby", &GameLobbyJoinRequested_t::m_steamIDLobby)
 		.def_readwrite("steamIDFriend", &GameLobbyJoinRequested_t::m_steamIDFriend)
 	;
 	py::class_<AvatarImageLoaded_t> _avatarimageloaded_t(m, "AvatarImageLoaded");
 	_avatarimageloaded_t
 		.def("_addr", [](AvatarImageLoaded_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("steamID", &AvatarImageLoaded_t::m_steamID)
 		.def_readwrite("iImage", &AvatarImageLoaded_t::m_iImage)
 		.def_readwrite("iWide", &AvatarImageLoaded_t::m_iWide)
 		.def_readwrite("iTall", &AvatarImageLoaded_t::m_iTall)
 	;
 	py::class_<ClanOfficerListResponse_t> _clanofficerlistresponse_t(m, "ClanOfficerListResponse");
 	_clanofficerlistresponse_t
 		.def("_addr", [](ClanOfficerListResponse_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("steamIDClan", &ClanOfficerListResponse_t::m_steamIDClan)
 		.def_readwrite("cOfficers", &ClanOfficerListResponse_t::m_cOfficers)
 		.def_readwrite("bSuccess", &ClanOfficerListResponse_t::m_bSuccess)
 	;
 	py::class_<FriendRichPresenceUpdate_t> _friendrichpresenceupdate_t(m, "FriendRichPresenceUpdate");
 	_friendrichpresenceupdate_t
 		.def("_addr", [](FriendRichPresenceUpdate_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("steamIDFriend", &FriendRichPresenceUpdate_t::m_steamIDFriend)
 		.def_readwrite("nAppID", &FriendRichPresenceUpdate_t::m_nAppID)
 	;
 	py::class_<GameRichPresenceJoinRequested_t> _gamerichpresencejoinrequested_t(m, "GameRichPresenceJoinRequested");
 	_gamerichpresencejoinrequested_t
 		.def("_addr", [](GameRichPresenceJoinRequested_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("steamIDFriend", &GameRichPresenceJoinRequested_t::m_steamIDFriend)
 		.def_property("rgchConnect",
			[](const GameRichPresenceJoinRequested_t& x) { return get_char_array(x.m_rgchConnect); },
			[](GameRichPresenceJoinRequested_t& x, const std::string& s) { set_char_array(x.m_rgchConnect, s); })
 	;
 	py::class_<GameConnectedClanChatMsg_t> _gameconnectedclanchatmsg_t(m, "GameConnectedClanChatMsg");
 	_gameconnectedclanchatmsg_t
 		.def("_addr", [](GameConnectedClanChatMsg_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("steamIDClanChat", &GameConnectedClanChatMsg_t::m_steamIDClanChat)
 		.def_readwrite("steamIDUser", &GameConnectedClanChatMsg_t::m_steamIDUser)
 		.def_readwrite("iMessageID", &GameConnectedClanChatMsg_t::m_iMessageID)
 	;
 	py::class_<GameConnectedChatJoin_t> _gameconnectedchatjoin_t(m, "GameConnectedChatJoin");
 	_gameconnectedchatjoin_t
 		.def("_addr", [](GameConnectedChatJoin_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("steamIDClanChat", &GameConnectedChatJoin_t::m_steamIDClanChat)
 		.def_readwrite("steamIDUser", &GameConnectedChatJoin_t::m_steamIDUser)
 	;
 	py::class_<GameConnectedChatLeave_t> _gameconnectedchatleave_t(m, "GameConnectedChatLeave");
 	_gameconnectedchatleave_t
 		.def("_addr", [](GameConnectedChatLeave_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("steamIDClanChat", &GameConnectedChatLeave_t::m_steamIDClanChat)
 		.def_readwrite("steamIDUser", &GameConnectedChatLeave_t::m_steamIDUser)
 		.def_readwrite("bKicked", &GameConnectedChatLeave_t::m_bKicked)
 		.def_readwrite("bDropped", &GameConnectedChatLeave_t::m_bDropped)
 	;
 	py::class_<DownloadClanActivityCountsResult_t> _downloadclanactivitycountsresult_t(m, "DownloadClanActivityCountsResult");
 	_downloadclanactivitycountsresult_t
 		.def("_addr", [](DownloadClanActivityCountsResult_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("bSuccess", &DownloadClanActivityCountsResult_t::m_bSuccess)
 	;
 	py::class_<JoinClanChatRoomCompletionResult_t> _joinclanchatroomcompletionresult_t(m, "JoinClanChatRoomCompletionResult");
 	_joinclanchatroomcompletionresult_t
 		.def("_addr", [](JoinClanChatRoomCompletionResult_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("steamIDClanChat", &JoinClanChatRoomCompletionResult_t::m_steamIDClanChat)
 		.def_readwrite("eChatRoomEnterResponse", &JoinClanChatRoomCompletionResult_t::m_eChatRoomEnterResponse)
 	;
 	py::class_<GameConnectedFriendChatMsg_t> _gameconnectedfriendchatmsg_t(m, "GameConnectedFriendChatMsg");
 	_gameconnectedfriendchatmsg_t
 		.def("_addr", [](GameConnectedFriendChatMsg_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("steamIDUser", &GameConnectedFriendChatMsg_t::m_steamIDUser)
 		.def_readwrite("iMessageID", &GameConnectedFriendChatMsg_t::m_iMessageID)
 	;
 	py::class_<FriendsGetFollowerCount_t> _friendsgetfollowercount_t(m, "FriendsGetFollowerCount");
 	_friendsgetfollowercount_t
 		.def("_addr", [](FriendsGetFollowerCount_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("eResult", &FriendsGetFollowerCount_t::m_eResult)
 		.def_readwrite("steamID", &FriendsGetFollowerCount_t::m_steamID)
 		.def_readwrite("nCount", &FriendsGetFollowerCount_t::m_nCount)
 	;
 	py::class_<FriendsIsFollowing_t> _friendsisfollowing_t(m, "FriendsIsFollowing");
 	_friendsisfollowing_t
 		.def("_addr", [](FriendsIsFollowing_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("eResult", &FriendsIsFollowing_t::m_eResult)
 		.def_readwrite("steamID", &FriendsIsFollowing_t::m_steamID)
 		.def_readwrite("bIsFollowing", &FriendsIsFollowing_t::m_bIsFollowing)
 	;
 	py::class_<FriendsEnumerateFollowingList_t> _friendsenumeratefollowinglist_t(m, "FriendsEnumerateFollowingList");
 	_friendsenumeratefollowinglist_t
 		.def("_addr", [](FriendsEnumerateFollowingList_t& self) { return reinterpret_cast<uintptr_t>(&self); })
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
 		.def("_addr", [](UnreadChatMessagesChanged_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 	;
 	py::class_<OverlayBrowserProtocolNavigation_t> _overlaybrowserprotocolnavigation_t(m, "OverlayBrowserProtocolNavigation");
 	_overlaybrowserprotocolnavigation_t
 		.def("_addr", [](OverlayBrowserProtocolNavigation_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_property("rgchURI",
			[](const OverlayBrowserProtocolNavigation_t& x) { return get_char_array(x.rgchURI); },
			[](OverlayBrowserProtocolNavigation_t& x, const std::string& s) { set_char_array(x.rgchURI, s); })
 	;
 	py::class_<EquippedProfileItemsChanged_t> _equippedprofileitemschanged_t(m, "EquippedProfileItemsChanged");
 	_equippedprofileitemschanged_t
 		.def("_addr", [](EquippedProfileItemsChanged_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("steamID", &EquippedProfileItemsChanged_t::m_steamID)
 	;
 	py::class_<EquippedProfileItems_t> _equippedprofileitems_t(m, "EquippedProfileItems");
 	_equippedprofileitems_t
 		.def("_addr", [](EquippedProfileItems_t& self) { return reinterpret_cast<uintptr_t>(&self); })
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
 		.def("_addr", [](IPCountry_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 	;
 	py::class_<LowBatteryPower_t> _lowbatterypower_t(m, "LowBatteryPower");
 	_lowbatterypower_t
 		.def("_addr", [](LowBatteryPower_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("nMinutesBatteryLeft", &LowBatteryPower_t::m_nMinutesBatteryLeft)
 	;
 	py::class_<SteamAPICallCompleted_t> _steamapicallcompleted_t(m, "SteamAPICallCompleted");
 	_steamapicallcompleted_t
 		.def("_addr", [](SteamAPICallCompleted_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("hAsyncCall", &SteamAPICallCompleted_t::m_hAsyncCall)
 		.def_readwrite("iCallback", &SteamAPICallCompleted_t::m_iCallback)
 		.def_readwrite("cubParam", &SteamAPICallCompleted_t::m_cubParam)
 	;
 	py::class_<SteamShutdown_t> _steamshutdown_t(m, "SteamShutdown");
 	_steamshutdown_t
 		.def("_addr", [](SteamShutdown_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 	;
 	py::class_<CheckFileSignature_t> _checkfilesignature_t(m, "CheckFileSignature");
 	_checkfilesignature_t
 		.def("_addr", [](CheckFileSignature_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("eCheckFileSignature", &CheckFileSignature_t::m_eCheckFileSignature)
 	;
 	py::class_<GamepadTextInputDismissed_t> _gamepadtextinputdismissed_t(m, "GamepadTextInputDismissed");
 	_gamepadtextinputdismissed_t
 		.def("_addr", [](GamepadTextInputDismissed_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("bSubmitted", &GamepadTextInputDismissed_t::m_bSubmitted)
 		.def_readwrite("unSubmittedText", &GamepadTextInputDismissed_t::m_unSubmittedText)
 		.def_readwrite("unAppID", &GamepadTextInputDismissed_t::m_unAppID)
 	;
 	py::class_<AppResumingFromSuspend_t> _appresumingfromsuspend_t(m, "AppResumingFromSuspend");
 	_appresumingfromsuspend_t
 		.def("_addr", [](AppResumingFromSuspend_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 	;
 	py::class_<FloatingGamepadTextInputDismissed_t> _floatinggamepadtextinputdismissed_t(m, "FloatingGamepadTextInputDismissed");
 	_floatinggamepadtextinputdismissed_t
 		.def("_addr", [](FloatingGamepadTextInputDismissed_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 	;
 	py::class_<FilterTextDictionaryChanged_t> _filtertextdictionarychanged_t(m, "FilterTextDictionaryChanged");
 	_filtertextdictionarychanged_t
 		.def("_addr", [](FilterTextDictionaryChanged_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("eLanguage", &FilterTextDictionaryChanged_t::m_eLanguage)
 	;
 	py::class_<FavoritesListChanged_t> _favoriteslistchanged_t(m, "FavoritesListChanged");
 	_favoriteslistchanged_t
 		.def("_addr", [](FavoritesListChanged_t& self) { return reinterpret_cast<uintptr_t>(&self); })
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
 		.def("_addr", [](LobbyInvite_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("ulSteamIDUser", &LobbyInvite_t::m_ulSteamIDUser)
 		.def_readwrite("ulSteamIDLobby", &LobbyInvite_t::m_ulSteamIDLobby)
 		.def_readwrite("ulGameID", &LobbyInvite_t::m_ulGameID)
 	;
 	py::class_<LobbyEnter_t> _lobbyenter_t(m, "LobbyEnter");
 	_lobbyenter_t
 		.def("_addr", [](LobbyEnter_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("ulSteamIDLobby", &LobbyEnter_t::m_ulSteamIDLobby)
 		.def_readwrite("rgfChatPermissions", &LobbyEnter_t::m_rgfChatPermissions)
 		.def_readwrite("bLocked", &LobbyEnter_t::m_bLocked)
 		.def_readwrite("EChatRoomEnterResponse", &LobbyEnter_t::m_EChatRoomEnterResponse)
 	;
 	py::class_<LobbyDataUpdate_t> _lobbydataupdate_t(m, "LobbyDataUpdate");
 	_lobbydataupdate_t
 		.def("_addr", [](LobbyDataUpdate_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("ulSteamIDLobby", &LobbyDataUpdate_t::m_ulSteamIDLobby)
 		.def_readwrite("ulSteamIDMember", &LobbyDataUpdate_t::m_ulSteamIDMember)
 		.def_readwrite("bSuccess", &LobbyDataUpdate_t::m_bSuccess)
 	;
 	py::class_<LobbyChatUpdate_t> _lobbychatupdate_t(m, "LobbyChatUpdate");
 	_lobbychatupdate_t
 		.def("_addr", [](LobbyChatUpdate_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("ulSteamIDLobby", &LobbyChatUpdate_t::m_ulSteamIDLobby)
 		.def_readwrite("ulSteamIDUserChanged", &LobbyChatUpdate_t::m_ulSteamIDUserChanged)
 		.def_readwrite("ulSteamIDMakingChange", &LobbyChatUpdate_t::m_ulSteamIDMakingChange)
 		.def_readwrite("rgfChatMemberStateChange", &LobbyChatUpdate_t::m_rgfChatMemberStateChange)
 	;
 	py::class_<LobbyChatMsg_t> _lobbychatmsg_t(m, "LobbyChatMsg");
 	_lobbychatmsg_t
 		.def("_addr", [](LobbyChatMsg_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("ulSteamIDLobby", &LobbyChatMsg_t::m_ulSteamIDLobby)
 		.def_readwrite("ulSteamIDUser", &LobbyChatMsg_t::m_ulSteamIDUser)
 		.def_readwrite("eChatEntryType", &LobbyChatMsg_t::m_eChatEntryType)
 		.def_readwrite("iChatID", &LobbyChatMsg_t::m_iChatID)
 	;
 	py::class_<LobbyGameCreated_t> _lobbygamecreated_t(m, "LobbyGameCreated");
 	_lobbygamecreated_t
 		.def("_addr", [](LobbyGameCreated_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("ulSteamIDLobby", &LobbyGameCreated_t::m_ulSteamIDLobby)
 		.def_readwrite("ulSteamIDGameServer", &LobbyGameCreated_t::m_ulSteamIDGameServer)
 		.def_readwrite("unIP", &LobbyGameCreated_t::m_unIP)
 		.def_readwrite("usPort", &LobbyGameCreated_t::m_usPort)
 	;
 	py::class_<LobbyMatchList_t> _lobbymatchlist_t(m, "LobbyMatchList");
 	_lobbymatchlist_t
 		.def("_addr", [](LobbyMatchList_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("nLobbiesMatching", &LobbyMatchList_t::m_nLobbiesMatching)
 	;
 	py::class_<LobbyKicked_t> _lobbykicked_t(m, "LobbyKicked");
 	_lobbykicked_t
 		.def("_addr", [](LobbyKicked_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("ulSteamIDLobby", &LobbyKicked_t::m_ulSteamIDLobby)
 		.def_readwrite("ulSteamIDAdmin", &LobbyKicked_t::m_ulSteamIDAdmin)
 		.def_readwrite("bKickedDueToDisconnect", &LobbyKicked_t::m_bKickedDueToDisconnect)
 	;
 	py::class_<LobbyCreated_t> _lobbycreated_t(m, "LobbyCreated");
 	_lobbycreated_t
 		.def("_addr", [](LobbyCreated_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("eResult", &LobbyCreated_t::m_eResult)
 		.def_readwrite("ulSteamIDLobby", &LobbyCreated_t::m_ulSteamIDLobby)
 	;
 	py::class_<FavoritesListAccountsUpdated_t> _favoriteslistaccountsupdated_t(m, "FavoritesListAccountsUpdated");
 	_favoriteslistaccountsupdated_t
 		.def("_addr", [](FavoritesListAccountsUpdated_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("eResult", &FavoritesListAccountsUpdated_t::m_eResult)
 	;
 	py::class_<JoinPartyCallback_t> _joinpartycallback_t(m, "JoinPartyCallback");
 	_joinpartycallback_t
 		.def("_addr", [](JoinPartyCallback_t& self) { return reinterpret_cast<uintptr_t>(&self); })
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
 		.def("_addr", [](CreateBeaconCallback_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("eResult", &CreateBeaconCallback_t::m_eResult)
 		.def_readwrite("ulBeaconID", &CreateBeaconCallback_t::m_ulBeaconID)
 	;
 	py::class_<ReservationNotificationCallback_t> _reservationnotificationcallback_t(m, "ReservationNotificationCallback");
 	_reservationnotificationcallback_t
 		.def("_addr", [](ReservationNotificationCallback_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("ulBeaconID", &ReservationNotificationCallback_t::m_ulBeaconID)
 		.def_readwrite("steamIDJoiner", &ReservationNotificationCallback_t::m_steamIDJoiner)
 	;
 	py::class_<ChangeNumOpenSlotsCallback_t> _changenumopenslotscallback_t(m, "ChangeNumOpenSlotsCallback");
 	_changenumopenslotscallback_t
 		.def("_addr", [](ChangeNumOpenSlotsCallback_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("eResult", &ChangeNumOpenSlotsCallback_t::m_eResult)
 	;
 	py::class_<AvailableBeaconLocationsUpdated_t> _availablebeaconlocationsupdated_t(m, "AvailableBeaconLocationsUpdated");
 	_availablebeaconlocationsupdated_t
 		.def("_addr", [](AvailableBeaconLocationsUpdated_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 	;
 	py::class_<ActiveBeaconsUpdated_t> _activebeaconsupdated_t(m, "ActiveBeaconsUpdated");
 	_activebeaconsupdated_t
 		.def("_addr", [](ActiveBeaconsUpdated_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 	;
 	py::class_<RemoteStorageFileShareResult_t> _remotestoragefileshareresult_t(m, "RemoteStorageFileShareResult");
 	_remotestoragefileshareresult_t
 		.def("_addr", [](RemoteStorageFileShareResult_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("eResult", &RemoteStorageFileShareResult_t::m_eResult)
 		.def_readwrite("hFile", &RemoteStorageFileShareResult_t::m_hFile)
 		.def_property("rgchFilename",
			[](const RemoteStorageFileShareResult_t& x) { return get_char_array(x.m_rgchFilename); },
			[](RemoteStorageFileShareResult_t& x, const std::string& s) { set_char_array(x.m_rgchFilename, s); })
 	;
 	py::class_<RemoteStoragePublishFileResult_t> _remotestoragepublishfileresult_t(m, "RemoteStoragePublishFileResult");
 	_remotestoragepublishfileresult_t
 		.def("_addr", [](RemoteStoragePublishFileResult_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("eResult", &RemoteStoragePublishFileResult_t::m_eResult)
 		.def_readwrite("nPublishedFileId", &RemoteStoragePublishFileResult_t::m_nPublishedFileId)
 		.def_readwrite("bUserNeedsToAcceptWorkshopLegalAgreement", &RemoteStoragePublishFileResult_t::m_bUserNeedsToAcceptWorkshopLegalAgreement)
 	;
 	py::class_<RemoteStorageDeletePublishedFileResult_t> _remotestoragedeletepublishedfileresult_t(m, "RemoteStorageDeletePublishedFileResult");
 	_remotestoragedeletepublishedfileresult_t
 		.def("_addr", [](RemoteStorageDeletePublishedFileResult_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("eResult", &RemoteStorageDeletePublishedFileResult_t::m_eResult)
 		.def_readwrite("nPublishedFileId", &RemoteStorageDeletePublishedFileResult_t::m_nPublishedFileId)
 	;
 	py::class_<RemoteStorageEnumerateUserPublishedFilesResult_t> _remotestorageenumerateuserpublishedfilesresult_t(m, "RemoteStorageEnumerateUserPublishedFilesResult");
 	_remotestorageenumerateuserpublishedfilesresult_t
 		.def("_addr", [](RemoteStorageEnumerateUserPublishedFilesResult_t& self) { return reinterpret_cast<uintptr_t>(&self); })
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
 		.def("_addr", [](RemoteStorageSubscribePublishedFileResult_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("eResult", &RemoteStorageSubscribePublishedFileResult_t::m_eResult)
 		.def_readwrite("nPublishedFileId", &RemoteStorageSubscribePublishedFileResult_t::m_nPublishedFileId)
 	;
 	py::class_<RemoteStorageEnumerateUserSubscribedFilesResult_t> _remotestorageenumerateusersubscribedfilesresult_t(m, "RemoteStorageEnumerateUserSubscribedFilesResult");
 	_remotestorageenumerateusersubscribedfilesresult_t
 		.def("_addr", [](RemoteStorageEnumerateUserSubscribedFilesResult_t& self) { return reinterpret_cast<uintptr_t>(&self); })
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
 		.def("_addr", [](RemoteStorageUnsubscribePublishedFileResult_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("eResult", &RemoteStorageUnsubscribePublishedFileResult_t::m_eResult)
 		.def_readwrite("nPublishedFileId", &RemoteStorageUnsubscribePublishedFileResult_t::m_nPublishedFileId)
 	;
 	py::class_<RemoteStorageUpdatePublishedFileResult_t> _remotestorageupdatepublishedfileresult_t(m, "RemoteStorageUpdatePublishedFileResult");
 	_remotestorageupdatepublishedfileresult_t
 		.def("_addr", [](RemoteStorageUpdatePublishedFileResult_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("eResult", &RemoteStorageUpdatePublishedFileResult_t::m_eResult)
 		.def_readwrite("nPublishedFileId", &RemoteStorageUpdatePublishedFileResult_t::m_nPublishedFileId)
 		.def_readwrite("bUserNeedsToAcceptWorkshopLegalAgreement", &RemoteStorageUpdatePublishedFileResult_t::m_bUserNeedsToAcceptWorkshopLegalAgreement)
 	;
 	py::class_<RemoteStorageDownloadUGCResult_t> _remotestoragedownloadugcresult_t(m, "RemoteStorageDownloadUGCResult");
 	_remotestoragedownloadugcresult_t
 		.def("_addr", [](RemoteStorageDownloadUGCResult_t& self) { return reinterpret_cast<uintptr_t>(&self); })
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
 		.def("_addr", [](RemoteStorageGetPublishedFileDetailsResult_t& self) { return reinterpret_cast<uintptr_t>(&self); })
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
 		.def("_addr", [](RemoteStorageEnumerateWorkshopFilesResult_t& self) { return reinterpret_cast<uintptr_t>(&self); })
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
 		.def("_addr", [](RemoteStorageGetPublishedItemVoteDetailsResult_t& self) { return reinterpret_cast<uintptr_t>(&self); })
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
 		.def("_addr", [](RemoteStoragePublishedFileSubscribed_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("nPublishedFileId", &RemoteStoragePublishedFileSubscribed_t::m_nPublishedFileId)
 		.def_readwrite("nAppID", &RemoteStoragePublishedFileSubscribed_t::m_nAppID)
 	;
 	py::class_<RemoteStoragePublishedFileUnsubscribed_t> _remotestoragepublishedfileunsubscribed_t(m, "RemoteStoragePublishedFileUnsubscribed");
 	_remotestoragepublishedfileunsubscribed_t
 		.def("_addr", [](RemoteStoragePublishedFileUnsubscribed_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("nPublishedFileId", &RemoteStoragePublishedFileUnsubscribed_t::m_nPublishedFileId)
 		.def_readwrite("nAppID", &RemoteStoragePublishedFileUnsubscribed_t::m_nAppID)
 	;
 	py::class_<RemoteStoragePublishedFileDeleted_t> _remotestoragepublishedfiledeleted_t(m, "RemoteStoragePublishedFileDeleted");
 	_remotestoragepublishedfiledeleted_t
 		.def("_addr", [](RemoteStoragePublishedFileDeleted_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("nPublishedFileId", &RemoteStoragePublishedFileDeleted_t::m_nPublishedFileId)
 		.def_readwrite("nAppID", &RemoteStoragePublishedFileDeleted_t::m_nAppID)
 	;
 	py::class_<RemoteStorageUpdateUserPublishedItemVoteResult_t> _remotestorageupdateuserpublisheditemvoteresult_t(m, "RemoteStorageUpdateUserPublishedItemVoteResult");
 	_remotestorageupdateuserpublisheditemvoteresult_t
 		.def("_addr", [](RemoteStorageUpdateUserPublishedItemVoteResult_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("eResult", &RemoteStorageUpdateUserPublishedItemVoteResult_t::m_eResult)
 		.def_readwrite("nPublishedFileId", &RemoteStorageUpdateUserPublishedItemVoteResult_t::m_nPublishedFileId)
 	;
 	py::class_<RemoteStorageUserVoteDetails_t> _remotestorageuservotedetails_t(m, "RemoteStorageUserVoteDetails");
 	_remotestorageuservotedetails_t
 		.def("_addr", [](RemoteStorageUserVoteDetails_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("eResult", &RemoteStorageUserVoteDetails_t::m_eResult)
 		.def_readwrite("nPublishedFileId", &RemoteStorageUserVoteDetails_t::m_nPublishedFileId)
 		.def_readwrite("eVote", &RemoteStorageUserVoteDetails_t::m_eVote)
 	;
 	py::class_<RemoteStorageEnumerateUserSharedWorkshopFilesResult_t> _remotestorageenumerateusersharedworkshopfilesresult_t(m, "RemoteStorageEnumerateUserSharedWorkshopFilesResult");
 	_remotestorageenumerateusersharedworkshopfilesresult_t
 		.def("_addr", [](RemoteStorageEnumerateUserSharedWorkshopFilesResult_t& self) { return reinterpret_cast<uintptr_t>(&self); })
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
 		.def("_addr", [](RemoteStorageSetUserPublishedFileActionResult_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("eResult", &RemoteStorageSetUserPublishedFileActionResult_t::m_eResult)
 		.def_readwrite("nPublishedFileId", &RemoteStorageSetUserPublishedFileActionResult_t::m_nPublishedFileId)
 		.def_readwrite("eAction", &RemoteStorageSetUserPublishedFileActionResult_t::m_eAction)
 	;
 	py::class_<RemoteStorageEnumeratePublishedFilesByUserActionResult_t> _remotestorageenumeratepublishedfilesbyuseractionresult_t(m, "RemoteStorageEnumeratePublishedFilesByUserActionResult");
 	_remotestorageenumeratepublishedfilesbyuseractionresult_t
 		.def("_addr", [](RemoteStorageEnumeratePublishedFilesByUserActionResult_t& self) { return reinterpret_cast<uintptr_t>(&self); })
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
 		.def("_addr", [](RemoteStoragePublishFileProgress_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("dPercentFile", &RemoteStoragePublishFileProgress_t::m_dPercentFile)
 		.def_readwrite("bPreview", &RemoteStoragePublishFileProgress_t::m_bPreview)
 	;
 	py::class_<RemoteStoragePublishedFileUpdated_t> _remotestoragepublishedfileupdated_t(m, "RemoteStoragePublishedFileUpdated");
 	_remotestoragepublishedfileupdated_t
 		.def("_addr", [](RemoteStoragePublishedFileUpdated_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("nPublishedFileId", &RemoteStoragePublishedFileUpdated_t::m_nPublishedFileId)
 		.def_readwrite("nAppID", &RemoteStoragePublishedFileUpdated_t::m_nAppID)
 		.def_readwrite("ulUnused", &RemoteStoragePublishedFileUpdated_t::m_ulUnused)
 	;
 	py::class_<RemoteStorageFileWriteAsyncComplete_t> _remotestoragefilewriteasynccomplete_t(m, "RemoteStorageFileWriteAsyncComplete");
 	_remotestoragefilewriteasynccomplete_t
 		.def("_addr", [](RemoteStorageFileWriteAsyncComplete_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("eResult", &RemoteStorageFileWriteAsyncComplete_t::m_eResult)
 	;
 	py::class_<RemoteStorageFileReadAsyncComplete_t> _remotestoragefilereadasynccomplete_t(m, "RemoteStorageFileReadAsyncComplete");
 	_remotestoragefilereadasynccomplete_t
 		.def("_addr", [](RemoteStorageFileReadAsyncComplete_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("hFileReadAsync", &RemoteStorageFileReadAsyncComplete_t::m_hFileReadAsync)
 		.def_readwrite("eResult", &RemoteStorageFileReadAsyncComplete_t::m_eResult)
 		.def_readwrite("nOffset", &RemoteStorageFileReadAsyncComplete_t::m_nOffset)
 		.def_readwrite("cubRead", &RemoteStorageFileReadAsyncComplete_t::m_cubRead)
 	;
 	py::class_<RemoteStorageLocalFileChange_t> _remotestoragelocalfilechange_t(m, "RemoteStorageLocalFileChange");
 	_remotestoragelocalfilechange_t
 		.def("_addr", [](RemoteStorageLocalFileChange_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 	;
 	py::class_<UserStatsReceived_t> _userstatsreceived_t(m, "UserStatsReceived");
 	_userstatsreceived_t
 		.def("_addr", [](UserStatsReceived_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("nGameID", &UserStatsReceived_t::m_nGameID)
 		.def_readwrite("eResult", &UserStatsReceived_t::m_eResult)
 		.def_readwrite("steamIDUser", &UserStatsReceived_t::m_steamIDUser)
 	;
 	py::class_<UserStatsStored_t> _userstatsstored_t(m, "UserStatsStored");
 	_userstatsstored_t
 		.def("_addr", [](UserStatsStored_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("nGameID", &UserStatsStored_t::m_nGameID)
 		.def_readwrite("eResult", &UserStatsStored_t::m_eResult)
 	;
 	py::class_<UserAchievementStored_t> _userachievementstored_t(m, "UserAchievementStored");
 	_userachievementstored_t
 		.def("_addr", [](UserAchievementStored_t& self) { return reinterpret_cast<uintptr_t>(&self); })
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
 		.def("_addr", [](LeaderboardFindResult_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("hSteamLeaderboard", &LeaderboardFindResult_t::m_hSteamLeaderboard)
 		.def_readwrite("bLeaderboardFound", &LeaderboardFindResult_t::m_bLeaderboardFound)
 	;
 	py::class_<LeaderboardScoresDownloaded_t> _leaderboardscoresdownloaded_t(m, "LeaderboardScoresDownloaded");
 	_leaderboardscoresdownloaded_t
 		.def("_addr", [](LeaderboardScoresDownloaded_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("hSteamLeaderboard", &LeaderboardScoresDownloaded_t::m_hSteamLeaderboard)
 		.def_readwrite("hSteamLeaderboardEntries", &LeaderboardScoresDownloaded_t::m_hSteamLeaderboardEntries)
 		.def_readwrite("cEntryCount", &LeaderboardScoresDownloaded_t::m_cEntryCount)
 	;
 	py::class_<LeaderboardScoreUploaded_t> _leaderboardscoreuploaded_t(m, "LeaderboardScoreUploaded");
 	_leaderboardscoreuploaded_t
 		.def("_addr", [](LeaderboardScoreUploaded_t& self) { return reinterpret_cast<uintptr_t>(&self); })
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
 		.def("_addr", [](NumberOfCurrentPlayers_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("bSuccess", &NumberOfCurrentPlayers_t::m_bSuccess)
 		.def_readwrite("cPlayers", &NumberOfCurrentPlayers_t::m_cPlayers)
 	;
 	py::class_<UserStatsUnloaded_t> _userstatsunloaded_t(m, "UserStatsUnloaded");
 	_userstatsunloaded_t
 		.def("_addr", [](UserStatsUnloaded_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("steamIDUser", &UserStatsUnloaded_t::m_steamIDUser)
 	;
 	py::class_<UserAchievementIconFetched_t> _userachievementiconfetched_t(m, "UserAchievementIconFetched");
 	_userachievementiconfetched_t
 		.def("_addr", [](UserAchievementIconFetched_t& self) { return reinterpret_cast<uintptr_t>(&self); })
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
 		.def("_addr", [](GlobalAchievementPercentagesReady_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("nGameID", &GlobalAchievementPercentagesReady_t::m_nGameID)
 		.def_readwrite("eResult", &GlobalAchievementPercentagesReady_t::m_eResult)
 	;
 	py::class_<LeaderboardUGCSet_t> _leaderboardugcset_t(m, "LeaderboardUGCSet");
 	_leaderboardugcset_t
 		.def("_addr", [](LeaderboardUGCSet_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("eResult", &LeaderboardUGCSet_t::m_eResult)
 		.def_readwrite("hSteamLeaderboard", &LeaderboardUGCSet_t::m_hSteamLeaderboard)
 	;
 	py::class_<GlobalStatsReceived_t> _globalstatsreceived_t(m, "GlobalStatsReceived");
 	_globalstatsreceived_t
 		.def("_addr", [](GlobalStatsReceived_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("nGameID", &GlobalStatsReceived_t::m_nGameID)
 		.def_readwrite("eResult", &GlobalStatsReceived_t::m_eResult)
 	;
 	py::class_<DlcInstalled_t> _dlcinstalled_t(m, "DlcInstalled");
 	_dlcinstalled_t
 		.def("_addr", [](DlcInstalled_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("nAppID", &DlcInstalled_t::m_nAppID)
 	;
 	py::class_<NewUrlLaunchParameters_t> _newurllaunchparameters_t(m, "NewUrlLaunchParameters");
 	_newurllaunchparameters_t
 		.def("_addr", [](NewUrlLaunchParameters_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 	;
 	py::class_<AppProofOfPurchaseKeyResponse_t> _appproofofpurchasekeyresponse_t(m, "AppProofOfPurchaseKeyResponse");
 	_appproofofpurchasekeyresponse_t
 		.def("_addr", [](AppProofOfPurchaseKeyResponse_t& self) { return reinterpret_cast<uintptr_t>(&self); })
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
 		.def("_addr", [](FileDetailsResult_t& self) { return reinterpret_cast<uintptr_t>(&self); })
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
 		.def("_addr", [](TimedTrialStatus_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("unAppID", &TimedTrialStatus_t::m_unAppID)
 		.def_readwrite("bIsOffline", &TimedTrialStatus_t::m_bIsOffline)
 		.def_readwrite("unSecondsAllowed", &TimedTrialStatus_t::m_unSecondsAllowed)
 		.def_readwrite("unSecondsPlayed", &TimedTrialStatus_t::m_unSecondsPlayed)
 	;
 	py::class_<P2PSessionRequest_t> _p2psessionrequest_t(m, "P2PSessionRequest");
 	_p2psessionrequest_t
 		.def("_addr", [](P2PSessionRequest_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("steamIDRemote", &P2PSessionRequest_t::m_steamIDRemote)
 	;
 	py::class_<P2PSessionConnectFail_t> _p2psessionconnectfail_t(m, "P2PSessionConnectFail");
 	_p2psessionconnectfail_t
 		.def("_addr", [](P2PSessionConnectFail_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("steamIDRemote", &P2PSessionConnectFail_t::m_steamIDRemote)
 		.def_readwrite("eP2PSessionError", &P2PSessionConnectFail_t::m_eP2PSessionError)
 	;
 	py::class_<SocketStatusCallback_t> _socketstatuscallback_t(m, "SocketStatusCallback");
 	_socketstatuscallback_t
 		.def("_addr", [](SocketStatusCallback_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("hSocket", &SocketStatusCallback_t::m_hSocket)
 		.def_readwrite("hListenSocket", &SocketStatusCallback_t::m_hListenSocket)
 		.def_readwrite("steamIDRemote", &SocketStatusCallback_t::m_steamIDRemote)
 		.def_readwrite("eSNetSocketState", &SocketStatusCallback_t::m_eSNetSocketState)
 	;
 	py::class_<ScreenshotReady_t> _screenshotready_t(m, "ScreenshotReady");
 	_screenshotready_t
 		.def("_addr", [](ScreenshotReady_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("hLocal", &ScreenshotReady_t::m_hLocal)
 		.def_readwrite("eResult", &ScreenshotReady_t::m_eResult)
 	;
 	py::class_<ScreenshotRequested_t> _screenshotrequested_t(m, "ScreenshotRequested");
 	_screenshotrequested_t
 		.def("_addr", [](ScreenshotRequested_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 	;
 	py::class_<PlaybackStatusHasChanged_t> _playbackstatushaschanged_t(m, "PlaybackStatusHasChanged");
 	_playbackstatushaschanged_t
 		.def("_addr", [](PlaybackStatusHasChanged_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 	;
 	py::class_<VolumeHasChanged_t> _volumehaschanged_t(m, "VolumeHasChanged");
 	_volumehaschanged_t
 		.def("_addr", [](VolumeHasChanged_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("flNewVolume", &VolumeHasChanged_t::m_flNewVolume)
 	;
 	py::class_<HTTPRequestCompleted_t> _httprequestcompleted_t(m, "HTTPRequestCompleted");
 	_httprequestcompleted_t
 		.def("_addr", [](HTTPRequestCompleted_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("hRequest", &HTTPRequestCompleted_t::m_hRequest)
 		.def_readwrite("ulContextValue", &HTTPRequestCompleted_t::m_ulContextValue)
 		.def_readwrite("bRequestSuccessful", &HTTPRequestCompleted_t::m_bRequestSuccessful)
 		.def_readwrite("eStatusCode", &HTTPRequestCompleted_t::m_eStatusCode)
 		.def_readwrite("unBodySize", &HTTPRequestCompleted_t::m_unBodySize)
 	;
 	py::class_<HTTPRequestHeadersReceived_t> _httprequestheadersreceived_t(m, "HTTPRequestHeadersReceived");
 	_httprequestheadersreceived_t
 		.def("_addr", [](HTTPRequestHeadersReceived_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("hRequest", &HTTPRequestHeadersReceived_t::m_hRequest)
 		.def_readwrite("ulContextValue", &HTTPRequestHeadersReceived_t::m_ulContextValue)
 	;
 	py::class_<HTTPRequestDataReceived_t> _httprequestdatareceived_t(m, "HTTPRequestDataReceived");
 	_httprequestdatareceived_t
 		.def("_addr", [](HTTPRequestDataReceived_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("hRequest", &HTTPRequestDataReceived_t::m_hRequest)
 		.def_readwrite("ulContextValue", &HTTPRequestDataReceived_t::m_ulContextValue)
 		.def_readwrite("cOffset", &HTTPRequestDataReceived_t::m_cOffset)
 		.def_readwrite("cBytesReceived", &HTTPRequestDataReceived_t::m_cBytesReceived)
 	;
 	py::class_<SteamInputDeviceConnected_t> _steaminputdeviceconnected_t(m, "SteamInputDeviceConnected");
 	_steaminputdeviceconnected_t
 		.def("_addr", [](SteamInputDeviceConnected_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("ulConnectedDeviceHandle", &SteamInputDeviceConnected_t::m_ulConnectedDeviceHandle)
 	;
 	py::class_<SteamInputDeviceDisconnected_t> _steaminputdevicedisconnected_t(m, "SteamInputDeviceDisconnected");
 	_steaminputdevicedisconnected_t
 		.def("_addr", [](SteamInputDeviceDisconnected_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("ulDisconnectedDeviceHandle", &SteamInputDeviceDisconnected_t::m_ulDisconnectedDeviceHandle)
 	;
 	py::class_<SteamInputConfigurationLoaded_t> _steaminputconfigurationloaded_t(m, "SteamInputConfigurationLoaded");
 	_steaminputconfigurationloaded_t
 		.def("_addr", [](SteamInputConfigurationLoaded_t& self) { return reinterpret_cast<uintptr_t>(&self); })
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
 		.def("_addr", [](SteamInputGamepadSlotChange_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("unAppID", &SteamInputGamepadSlotChange_t::m_unAppID)
 		.def_readwrite("ulDeviceHandle", &SteamInputGamepadSlotChange_t::m_ulDeviceHandle)
 		.def_readwrite("eDeviceType", &SteamInputGamepadSlotChange_t::m_eDeviceType)
 		.def_readwrite("nOldGamepadSlot", &SteamInputGamepadSlotChange_t::m_nOldGamepadSlot)
 		.def_readwrite("nNewGamepadSlot", &SteamInputGamepadSlotChange_t::m_nNewGamepadSlot)
 	;
 	py::class_<SteamUGCQueryCompleted_t> _steamugcquerycompleted_t(m, "SteamUGCQueryCompleted");
 	_steamugcquerycompleted_t
 		.def("_addr", [](SteamUGCQueryCompleted_t& self) { return reinterpret_cast<uintptr_t>(&self); })
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
 		.def("_addr", [](SteamUGCRequestUGCDetailsResult_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("details", &SteamUGCRequestUGCDetailsResult_t::m_details)
 		.def_readwrite("bCachedData", &SteamUGCRequestUGCDetailsResult_t::m_bCachedData)
 	;
 	py::class_<CreateItemResult_t> _createitemresult_t(m, "CreateItemResult");
 	_createitemresult_t
 		.def("_addr", [](CreateItemResult_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("eResult", &CreateItemResult_t::m_eResult)
 		.def_readwrite("nPublishedFileId", &CreateItemResult_t::m_nPublishedFileId)
 		.def_readwrite("bUserNeedsToAcceptWorkshopLegalAgreement", &CreateItemResult_t::m_bUserNeedsToAcceptWorkshopLegalAgreement)
 	;
 	py::class_<SubmitItemUpdateResult_t> _submititemupdateresult_t(m, "SubmitItemUpdateResult");
 	_submititemupdateresult_t
 		.def("_addr", [](SubmitItemUpdateResult_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("eResult", &SubmitItemUpdateResult_t::m_eResult)
 		.def_readwrite("bUserNeedsToAcceptWorkshopLegalAgreement", &SubmitItemUpdateResult_t::m_bUserNeedsToAcceptWorkshopLegalAgreement)
 		.def_readwrite("nPublishedFileId", &SubmitItemUpdateResult_t::m_nPublishedFileId)
 	;
 	py::class_<ItemInstalled_t> _iteminstalled_t(m, "ItemInstalled");
 	_iteminstalled_t
 		.def("_addr", [](ItemInstalled_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("unAppID", &ItemInstalled_t::m_unAppID)
 		.def_readwrite("nPublishedFileId", &ItemInstalled_t::m_nPublishedFileId)
 		.def_readwrite("hLegacyContent", &ItemInstalled_t::m_hLegacyContent)
 		.def_readwrite("unManifestID", &ItemInstalled_t::m_unManifestID)
 	;
 	py::class_<DownloadItemResult_t> _downloaditemresult_t(m, "DownloadItemResult");
 	_downloaditemresult_t
 		.def("_addr", [](DownloadItemResult_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("unAppID", &DownloadItemResult_t::m_unAppID)
 		.def_readwrite("nPublishedFileId", &DownloadItemResult_t::m_nPublishedFileId)
 		.def_readwrite("eResult", &DownloadItemResult_t::m_eResult)
 	;
 	py::class_<UserFavoriteItemsListChanged_t> _userfavoriteitemslistchanged_t(m, "UserFavoriteItemsListChanged");
 	_userfavoriteitemslistchanged_t
 		.def("_addr", [](UserFavoriteItemsListChanged_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("nPublishedFileId", &UserFavoriteItemsListChanged_t::m_nPublishedFileId)
 		.def_readwrite("eResult", &UserFavoriteItemsListChanged_t::m_eResult)
 		.def_readwrite("bWasAddRequest", &UserFavoriteItemsListChanged_t::m_bWasAddRequest)
 	;
 	py::class_<SetUserItemVoteResult_t> _setuseritemvoteresult_t(m, "SetUserItemVoteResult");
 	_setuseritemvoteresult_t
 		.def("_addr", [](SetUserItemVoteResult_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("nPublishedFileId", &SetUserItemVoteResult_t::m_nPublishedFileId)
 		.def_readwrite("eResult", &SetUserItemVoteResult_t::m_eResult)
 		.def_readwrite("bVoteUp", &SetUserItemVoteResult_t::m_bVoteUp)
 	;
 	py::class_<GetUserItemVoteResult_t> _getuseritemvoteresult_t(m, "GetUserItemVoteResult");
 	_getuseritemvoteresult_t
 		.def("_addr", [](GetUserItemVoteResult_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("nPublishedFileId", &GetUserItemVoteResult_t::m_nPublishedFileId)
 		.def_readwrite("eResult", &GetUserItemVoteResult_t::m_eResult)
 		.def_readwrite("bVotedUp", &GetUserItemVoteResult_t::m_bVotedUp)
 		.def_readwrite("bVotedDown", &GetUserItemVoteResult_t::m_bVotedDown)
 		.def_readwrite("bVoteSkipped", &GetUserItemVoteResult_t::m_bVoteSkipped)
 	;
 	py::class_<StartPlaytimeTrackingResult_t> _startplaytimetrackingresult_t(m, "StartPlaytimeTrackingResult");
 	_startplaytimetrackingresult_t
 		.def("_addr", [](StartPlaytimeTrackingResult_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("eResult", &StartPlaytimeTrackingResult_t::m_eResult)
 	;
 	py::class_<StopPlaytimeTrackingResult_t> _stopplaytimetrackingresult_t(m, "StopPlaytimeTrackingResult");
 	_stopplaytimetrackingresult_t
 		.def("_addr", [](StopPlaytimeTrackingResult_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("eResult", &StopPlaytimeTrackingResult_t::m_eResult)
 	;
 	py::class_<AddUGCDependencyResult_t> _addugcdependencyresult_t(m, "AddUGCDependencyResult");
 	_addugcdependencyresult_t
 		.def("_addr", [](AddUGCDependencyResult_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("eResult", &AddUGCDependencyResult_t::m_eResult)
 		.def_readwrite("nPublishedFileId", &AddUGCDependencyResult_t::m_nPublishedFileId)
 		.def_readwrite("nChildPublishedFileId", &AddUGCDependencyResult_t::m_nChildPublishedFileId)
 	;
 	py::class_<RemoveUGCDependencyResult_t> _removeugcdependencyresult_t(m, "RemoveUGCDependencyResult");
 	_removeugcdependencyresult_t
 		.def("_addr", [](RemoveUGCDependencyResult_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("eResult", &RemoveUGCDependencyResult_t::m_eResult)
 		.def_readwrite("nPublishedFileId", &RemoveUGCDependencyResult_t::m_nPublishedFileId)
 		.def_readwrite("nChildPublishedFileId", &RemoveUGCDependencyResult_t::m_nChildPublishedFileId)
 	;
 	py::class_<AddAppDependencyResult_t> _addappdependencyresult_t(m, "AddAppDependencyResult");
 	_addappdependencyresult_t
 		.def("_addr", [](AddAppDependencyResult_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("eResult", &AddAppDependencyResult_t::m_eResult)
 		.def_readwrite("nPublishedFileId", &AddAppDependencyResult_t::m_nPublishedFileId)
 		.def_readwrite("nAppID", &AddAppDependencyResult_t::m_nAppID)
 	;
 	py::class_<RemoveAppDependencyResult_t> _removeappdependencyresult_t(m, "RemoveAppDependencyResult");
 	_removeappdependencyresult_t
 		.def("_addr", [](RemoveAppDependencyResult_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("eResult", &RemoveAppDependencyResult_t::m_eResult)
 		.def_readwrite("nPublishedFileId", &RemoveAppDependencyResult_t::m_nPublishedFileId)
 		.def_readwrite("nAppID", &RemoveAppDependencyResult_t::m_nAppID)
 	;
 	py::class_<GetAppDependenciesResult_t> _getappdependenciesresult_t(m, "GetAppDependenciesResult");
 	_getappdependenciesresult_t
 		.def("_addr", [](GetAppDependenciesResult_t& self) { return reinterpret_cast<uintptr_t>(&self); })
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
 		.def("_addr", [](DeleteItemResult_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("eResult", &DeleteItemResult_t::m_eResult)
 		.def_readwrite("nPublishedFileId", &DeleteItemResult_t::m_nPublishedFileId)
 	;
 	py::class_<UserSubscribedItemsListChanged_t> _usersubscribeditemslistchanged_t(m, "UserSubscribedItemsListChanged");
 	_usersubscribeditemslistchanged_t
 		.def("_addr", [](UserSubscribedItemsListChanged_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("nAppID", &UserSubscribedItemsListChanged_t::m_nAppID)
 	;
 	py::class_<WorkshopEULAStatus_t> _workshopeulastatus_t(m, "WorkshopEULAStatus");
 	_workshopeulastatus_t
 		.def("_addr", [](WorkshopEULAStatus_t& self) { return reinterpret_cast<uintptr_t>(&self); })
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
 		.def("_addr", [](HTML_BrowserReady_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("unBrowserHandle", &HTML_BrowserReady_t::unBrowserHandle)
 	;
 	py::class_<HTML_NeedsPaint_t> _html_needspaint_t(m, "HTML_NeedsPaint");
 	_html_needspaint_t
 		.def("_addr", [](HTML_NeedsPaint_t& self) { return reinterpret_cast<uintptr_t>(&self); })
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
 		.def("_addr", [](HTML_StartRequest_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("unBrowserHandle", &HTML_StartRequest_t::unBrowserHandle)
 		.def_readwrite("pchURL", &HTML_StartRequest_t::pchURL)
 		.def_readwrite("pchTarget", &HTML_StartRequest_t::pchTarget)
 		.def_readwrite("pchPostData", &HTML_StartRequest_t::pchPostData)
 		.def_readwrite("bIsRedirect", &HTML_StartRequest_t::bIsRedirect)
 	;
 	py::class_<HTML_CloseBrowser_t> _html_closebrowser_t(m, "HTML_CloseBrowser");
 	_html_closebrowser_t
 		.def("_addr", [](HTML_CloseBrowser_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("unBrowserHandle", &HTML_CloseBrowser_t::unBrowserHandle)
 	;
 	py::class_<HTML_URLChanged_t> _html_urlchanged_t(m, "HTML_URLChanged");
 	_html_urlchanged_t
 		.def("_addr", [](HTML_URLChanged_t& self) { return reinterpret_cast<uintptr_t>(&self); })
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
 		.def("_addr", [](HTML_FinishedRequest_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("unBrowserHandle", &HTML_FinishedRequest_t::unBrowserHandle)
 		.def_readwrite("pchURL", &HTML_FinishedRequest_t::pchURL)
 		.def_readwrite("pchPageTitle", &HTML_FinishedRequest_t::pchPageTitle)
 	;
 	py::class_<HTML_OpenLinkInNewTab_t> _html_openlinkinnewtab_t(m, "HTML_OpenLinkInNewTab");
 	_html_openlinkinnewtab_t
 		.def("_addr", [](HTML_OpenLinkInNewTab_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("unBrowserHandle", &HTML_OpenLinkInNewTab_t::unBrowserHandle)
 		.def_readwrite("pchURL", &HTML_OpenLinkInNewTab_t::pchURL)
 	;
 	py::class_<HTML_ChangedTitle_t> _html_changedtitle_t(m, "HTML_ChangedTitle");
 	_html_changedtitle_t
 		.def("_addr", [](HTML_ChangedTitle_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("unBrowserHandle", &HTML_ChangedTitle_t::unBrowserHandle)
 		.def_readwrite("pchTitle", &HTML_ChangedTitle_t::pchTitle)
 	;
 	py::class_<HTML_SearchResults_t> _html_searchresults_t(m, "HTML_SearchResults");
 	_html_searchresults_t
 		.def("_addr", [](HTML_SearchResults_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("unBrowserHandle", &HTML_SearchResults_t::unBrowserHandle)
 		.def_readwrite("unResults", &HTML_SearchResults_t::unResults)
 		.def_readwrite("unCurrentMatch", &HTML_SearchResults_t::unCurrentMatch)
 	;
 	py::class_<HTML_CanGoBackAndForward_t> _html_cangobackandforward_t(m, "HTML_CanGoBackAndForward");
 	_html_cangobackandforward_t
 		.def("_addr", [](HTML_CanGoBackAndForward_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("unBrowserHandle", &HTML_CanGoBackAndForward_t::unBrowserHandle)
 		.def_readwrite("bCanGoBack", &HTML_CanGoBackAndForward_t::bCanGoBack)
 		.def_readwrite("bCanGoForward", &HTML_CanGoBackAndForward_t::bCanGoForward)
 	;
 	py::class_<HTML_HorizontalScroll_t> _html_horizontalscroll_t(m, "HTML_HorizontalScroll");
 	_html_horizontalscroll_t
 		.def("_addr", [](HTML_HorizontalScroll_t& self) { return reinterpret_cast<uintptr_t>(&self); })
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
 		.def("_addr", [](HTML_VerticalScroll_t& self) { return reinterpret_cast<uintptr_t>(&self); })
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
 		.def("_addr", [](HTML_LinkAtPosition_t& self) { return reinterpret_cast<uintptr_t>(&self); })
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
 		.def("_addr", [](HTML_JSAlert_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("unBrowserHandle", &HTML_JSAlert_t::unBrowserHandle)
 		.def_readwrite("pchMessage", &HTML_JSAlert_t::pchMessage)
 	;
 	py::class_<HTML_JSConfirm_t> _html_jsconfirm_t(m, "HTML_JSConfirm");
 	_html_jsconfirm_t
 		.def("_addr", [](HTML_JSConfirm_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("unBrowserHandle", &HTML_JSConfirm_t::unBrowserHandle)
 		.def_readwrite("pchMessage", &HTML_JSConfirm_t::pchMessage)
 	;
 	py::class_<HTML_FileOpenDialog_t> _html_fileopendialog_t(m, "HTML_FileOpenDialog");
 	_html_fileopendialog_t
 		.def("_addr", [](HTML_FileOpenDialog_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("unBrowserHandle", &HTML_FileOpenDialog_t::unBrowserHandle)
 		.def_readwrite("pchTitle", &HTML_FileOpenDialog_t::pchTitle)
 		.def_readwrite("pchInitialFile", &HTML_FileOpenDialog_t::pchInitialFile)
 	;
 	py::class_<HTML_NewWindow_t> _html_newwindow_t(m, "HTML_NewWindow");
 	_html_newwindow_t
 		.def("_addr", [](HTML_NewWindow_t& self) { return reinterpret_cast<uintptr_t>(&self); })
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
 		.def("_addr", [](HTML_SetCursor_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("unBrowserHandle", &HTML_SetCursor_t::unBrowserHandle)
 		.def_readwrite("eMouseCursor", &HTML_SetCursor_t::eMouseCursor)
 	;
 	py::class_<HTML_StatusText_t> _html_statustext_t(m, "HTML_StatusText");
 	_html_statustext_t
 		.def("_addr", [](HTML_StatusText_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("unBrowserHandle", &HTML_StatusText_t::unBrowserHandle)
 		.def_readwrite("pchMsg", &HTML_StatusText_t::pchMsg)
 	;
 	py::class_<HTML_ShowToolTip_t> _html_showtooltip_t(m, "HTML_ShowToolTip");
 	_html_showtooltip_t
 		.def("_addr", [](HTML_ShowToolTip_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("unBrowserHandle", &HTML_ShowToolTip_t::unBrowserHandle)
 		.def_readwrite("pchMsg", &HTML_ShowToolTip_t::pchMsg)
 	;
 	py::class_<HTML_UpdateToolTip_t> _html_updatetooltip_t(m, "HTML_UpdateToolTip");
 	_html_updatetooltip_t
 		.def("_addr", [](HTML_UpdateToolTip_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("unBrowserHandle", &HTML_UpdateToolTip_t::unBrowserHandle)
 		.def_readwrite("pchMsg", &HTML_UpdateToolTip_t::pchMsg)
 	;
 	py::class_<HTML_HideToolTip_t> _html_hidetooltip_t(m, "HTML_HideToolTip");
 	_html_hidetooltip_t
 		.def("_addr", [](HTML_HideToolTip_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("unBrowserHandle", &HTML_HideToolTip_t::unBrowserHandle)
 	;
 	py::class_<HTML_BrowserRestarted_t> _html_browserrestarted_t(m, "HTML_BrowserRestarted");
 	_html_browserrestarted_t
 		.def("_addr", [](HTML_BrowserRestarted_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("unBrowserHandle", &HTML_BrowserRestarted_t::unBrowserHandle)
 		.def_readwrite("unOldBrowserHandle", &HTML_BrowserRestarted_t::unOldBrowserHandle)
 	;
 	py::class_<SteamInventoryResultReady_t> _steaminventoryresultready_t(m, "SteamInventoryResultReady");
 	_steaminventoryresultready_t
 		.def("_addr", [](SteamInventoryResultReady_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("handle", &SteamInventoryResultReady_t::m_handle)
 		.def_readwrite("result", &SteamInventoryResultReady_t::m_result)
 	;
 	py::class_<SteamInventoryFullUpdate_t> _steaminventoryfullupdate_t(m, "SteamInventoryFullUpdate");
 	_steaminventoryfullupdate_t
 		.def("_addr", [](SteamInventoryFullUpdate_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("handle", &SteamInventoryFullUpdate_t::m_handle)
 	;
 	py::class_<SteamInventoryDefinitionUpdate_t> _steaminventorydefinitionupdate_t(m, "SteamInventoryDefinitionUpdate");
 	_steaminventorydefinitionupdate_t
 		.def("_addr", [](SteamInventoryDefinitionUpdate_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 	;
 	py::class_<SteamInventoryEligiblePromoItemDefIDs_t> _steaminventoryeligiblepromoitemdefids_t(m, "SteamInventoryEligiblePromoItemDefIDs");
 	_steaminventoryeligiblepromoitemdefids_t
 		.def("_addr", [](SteamInventoryEligiblePromoItemDefIDs_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("result", &SteamInventoryEligiblePromoItemDefIDs_t::m_result)
 		.def_readwrite("steamID", &SteamInventoryEligiblePromoItemDefIDs_t::m_steamID)
 		.def_readwrite("numEligiblePromoItemDefs", &SteamInventoryEligiblePromoItemDefIDs_t::m_numEligiblePromoItemDefs)
 		.def_readwrite("bCachedData", &SteamInventoryEligiblePromoItemDefIDs_t::m_bCachedData)
 	;
 	py::class_<SteamInventoryStartPurchaseResult_t> _steaminventorystartpurchaseresult_t(m, "SteamInventoryStartPurchaseResult");
 	_steaminventorystartpurchaseresult_t
 		.def("_addr", [](SteamInventoryStartPurchaseResult_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("result", &SteamInventoryStartPurchaseResult_t::m_result)
 		.def_readwrite("ulOrderID", &SteamInventoryStartPurchaseResult_t::m_ulOrderID)
 		.def_readwrite("ulTransID", &SteamInventoryStartPurchaseResult_t::m_ulTransID)
 	;
 	py::class_<SteamInventoryRequestPricesResult_t> _steaminventoryrequestpricesresult_t(m, "SteamInventoryRequestPricesResult");
 	_steaminventoryrequestpricesresult_t
 		.def("_addr", [](SteamInventoryRequestPricesResult_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("result", &SteamInventoryRequestPricesResult_t::m_result)
 		.def_property("rgchCurrency",
			[](const SteamInventoryRequestPricesResult_t& x) { return get_char_array(x.m_rgchCurrency); },
			[](SteamInventoryRequestPricesResult_t& x, const std::string& s) { set_char_array(x.m_rgchCurrency, s); })
 	;
 	py::class_<SteamTimelineGamePhaseRecordingExists_t> _steamtimelinegamephaserecordingexists_t(m, "SteamTimelineGamePhaseRecordingExists");
 	_steamtimelinegamephaserecordingexists_t
 		.def("_addr", [](SteamTimelineGamePhaseRecordingExists_t& self) { return reinterpret_cast<uintptr_t>(&self); })
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
 		.def("_addr", [](SteamTimelineEventRecordingExists_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("ulEventID", &SteamTimelineEventRecordingExists_t::m_ulEventID)
 		.def_readwrite("bRecordingExists", &SteamTimelineEventRecordingExists_t::m_bRecordingExists)
 	;
 	py::class_<GetVideoURLResult_t> _getvideourlresult_t(m, "GetVideoURLResult");
 	_getvideourlresult_t
 		.def("_addr", [](GetVideoURLResult_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("eResult", &GetVideoURLResult_t::m_eResult)
 		.def_readwrite("unVideoAppID", &GetVideoURLResult_t::m_unVideoAppID)
 		.def_property("rgchURL",
			[](const GetVideoURLResult_t& x) { return get_char_array(x.m_rgchURL); },
			[](GetVideoURLResult_t& x, const std::string& s) { set_char_array(x.m_rgchURL, s); })
 	;
 	py::class_<GetOPFSettingsResult_t> _getopfsettingsresult_t(m, "GetOPFSettingsResult");
 	_getopfsettingsresult_t
 		.def("_addr", [](GetOPFSettingsResult_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("eResult", &GetOPFSettingsResult_t::m_eResult)
 		.def_readwrite("unVideoAppID", &GetOPFSettingsResult_t::m_unVideoAppID)
 	;
 	py::class_<BroadcastUploadStart_t> _broadcastuploadstart_t(m, "BroadcastUploadStart");
 	_broadcastuploadstart_t
 		.def("_addr", [](BroadcastUploadStart_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("bIsRTMP", &BroadcastUploadStart_t::m_bIsRTMP)
 	;
 	py::class_<BroadcastUploadStop_t> _broadcastuploadstop_t(m, "BroadcastUploadStop");
 	_broadcastuploadstop_t
 		.def("_addr", [](BroadcastUploadStop_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("eResult", &BroadcastUploadStop_t::m_eResult)
 	;
 	py::class_<SteamParentalSettingsChanged_t> _steamparentalsettingschanged_t(m, "SteamParentalSettingsChanged");
 	_steamparentalsettingschanged_t
 		.def("_addr", [](SteamParentalSettingsChanged_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 	;
 	py::class_<SteamRemotePlaySessionConnected_t> _steamremoteplaysessionconnected_t(m, "SteamRemotePlaySessionConnected");
 	_steamremoteplaysessionconnected_t
 		.def("_addr", [](SteamRemotePlaySessionConnected_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("unSessionID", &SteamRemotePlaySessionConnected_t::m_unSessionID)
 	;
 	py::class_<SteamRemotePlaySessionDisconnected_t> _steamremoteplaysessiondisconnected_t(m, "SteamRemotePlaySessionDisconnected");
 	_steamremoteplaysessiondisconnected_t
 		.def("_addr", [](SteamRemotePlaySessionDisconnected_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("unSessionID", &SteamRemotePlaySessionDisconnected_t::m_unSessionID)
 	;
 	py::class_<SteamRemotePlayTogetherGuestInvite_t> _steamremoteplaytogetherguestinvite_t(m, "SteamRemotePlayTogetherGuestInvite");
 	_steamremoteplaytogetherguestinvite_t
 		.def("_addr", [](SteamRemotePlayTogetherGuestInvite_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_property("szConnectURL",
			[](const SteamRemotePlayTogetherGuestInvite_t& x) { return get_char_array(x.m_szConnectURL); },
			[](SteamRemotePlayTogetherGuestInvite_t& x, const std::string& s) { set_char_array(x.m_szConnectURL, s); })
 	;
 	py::class_<SteamRemotePlaySessionAvatarLoaded_t> _steamremoteplaysessionavatarloaded_t(m, "SteamRemotePlaySessionAvatarLoaded");
 	_steamremoteplaysessionavatarloaded_t
 		.def("_addr", [](SteamRemotePlaySessionAvatarLoaded_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("unSessionID", &SteamRemotePlaySessionAvatarLoaded_t::m_unSessionID)
 		.def_readwrite("iImage", &SteamRemotePlaySessionAvatarLoaded_t::m_iImage)
 		.def_readwrite("iWide", &SteamRemotePlaySessionAvatarLoaded_t::m_iWide)
 		.def_readwrite("iTall", &SteamRemotePlaySessionAvatarLoaded_t::m_iTall)
 	;
 	py::class_<SteamNetworkingMessagesSessionRequest_t> _steamnetworkingmessagessessionrequest_t(m, "SteamNetworkingMessagesSessionRequest");
 	_steamnetworkingmessagessessionrequest_t
 		.def("_addr", [](SteamNetworkingMessagesSessionRequest_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("identityRemote", &SteamNetworkingMessagesSessionRequest_t::m_identityRemote)
 	;
 	py::class_<SteamNetworkingMessagesSessionFailed_t> _steamnetworkingmessagessessionfailed_t(m, "SteamNetworkingMessagesSessionFailed");
 	_steamnetworkingmessagessessionfailed_t
 		.def("_addr", [](SteamNetworkingMessagesSessionFailed_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("info", &SteamNetworkingMessagesSessionFailed_t::m_info)
 	;
 	py::class_<SteamNetConnectionStatusChangedCallback_t> _steamnetconnectionstatuschangedcallback_t(m, "SteamNetConnectionStatusChangedCallback");
 	_steamnetconnectionstatuschangedcallback_t
 		.def("_addr", [](SteamNetConnectionStatusChangedCallback_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("hConn", &SteamNetConnectionStatusChangedCallback_t::m_hConn)
 		.def_readwrite("info", &SteamNetConnectionStatusChangedCallback_t::m_info)
 		.def_readwrite("eOldState", &SteamNetConnectionStatusChangedCallback_t::m_eOldState)
 	;
 	py::class_<SteamNetAuthenticationStatus_t> _steamnetauthenticationstatus_t(m, "SteamNetAuthenticationStatus");
 	_steamnetauthenticationstatus_t
 		.def("_addr", [](SteamNetAuthenticationStatus_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("eAvail", &SteamNetAuthenticationStatus_t::m_eAvail)
 		.def_property("debugMsg",
			[](const SteamNetAuthenticationStatus_t& x) { return get_char_array(x.m_debugMsg); },
			[](SteamNetAuthenticationStatus_t& x, const std::string& s) { set_char_array(x.m_debugMsg, s); })
 	;
 	py::class_<SteamRelayNetworkStatus_t> _steamrelaynetworkstatus_t(m, "SteamRelayNetworkStatus");
 	_steamrelaynetworkstatus_t
 		.def("_addr", [](SteamRelayNetworkStatus_t& self) { return reinterpret_cast<uintptr_t>(&self); })
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
 		.def("_addr", [](GSClientApprove_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("SteamID", &GSClientApprove_t::m_SteamID)
 		.def_readwrite("OwnerSteamID", &GSClientApprove_t::m_OwnerSteamID)
 	;
 	py::class_<GSClientDeny_t> _gsclientdeny_t(m, "GSClientDeny");
 	_gsclientdeny_t
 		.def("_addr", [](GSClientDeny_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("SteamID", &GSClientDeny_t::m_SteamID)
 		.def_readwrite("eDenyReason", &GSClientDeny_t::m_eDenyReason)
 		.def_property("rgchOptionalText",
			[](const GSClientDeny_t& x) { return get_char_array(x.m_rgchOptionalText); },
			[](GSClientDeny_t& x, const std::string& s) { set_char_array(x.m_rgchOptionalText, s); })
 	;
 	py::class_<GSClientKick_t> _gsclientkick_t(m, "GSClientKick");
 	_gsclientkick_t
 		.def("_addr", [](GSClientKick_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("SteamID", &GSClientKick_t::m_SteamID)
 		.def_readwrite("eDenyReason", &GSClientKick_t::m_eDenyReason)
 	;
 	py::class_<GSClientAchievementStatus_t> _gsclientachievementstatus_t(m, "GSClientAchievementStatus");
 	_gsclientachievementstatus_t
 		.def("_addr", [](GSClientAchievementStatus_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("SteamID", &GSClientAchievementStatus_t::m_SteamID)
 		.def_property("pchAchievement",
			[](const GSClientAchievementStatus_t& x) { return get_char_array(x.m_pchAchievement); },
			[](GSClientAchievementStatus_t& x, const std::string& s) { set_char_array(x.m_pchAchievement, s); })
 		.def_readwrite("bUnlocked", &GSClientAchievementStatus_t::m_bUnlocked)
 	;
 	py::class_<GSPolicyResponse_t> _gspolicyresponse_t(m, "GSPolicyResponse");
 	_gspolicyresponse_t
 		.def("_addr", [](GSPolicyResponse_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("bSecure", &GSPolicyResponse_t::m_bSecure)
 	;
 	py::class_<GSGameplayStats_t> _gsgameplaystats_t(m, "GSGameplayStats");
 	_gsgameplaystats_t
 		.def("_addr", [](GSGameplayStats_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("eResult", &GSGameplayStats_t::m_eResult)
 		.def_readwrite("nRank", &GSGameplayStats_t::m_nRank)
 		.def_readwrite("unTotalConnects", &GSGameplayStats_t::m_unTotalConnects)
 		.def_readwrite("unTotalMinutesPlayed", &GSGameplayStats_t::m_unTotalMinutesPlayed)
 	;
 	py::class_<GSClientGroupStatus_t> _gsclientgroupstatus_t(m, "GSClientGroupStatus");
 	_gsclientgroupstatus_t
 		.def("_addr", [](GSClientGroupStatus_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("SteamIDUser", &GSClientGroupStatus_t::m_SteamIDUser)
 		.def_readwrite("SteamIDGroup", &GSClientGroupStatus_t::m_SteamIDGroup)
 		.def_readwrite("bMember", &GSClientGroupStatus_t::m_bMember)
 		.def_readwrite("bOfficer", &GSClientGroupStatus_t::m_bOfficer)
 	;
 	py::class_<GSReputation_t> _gsreputation_t(m, "GSReputation");
 	_gsreputation_t
 		.def("_addr", [](GSReputation_t& self) { return reinterpret_cast<uintptr_t>(&self); })
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
 		.def("_addr", [](AssociateWithClanResult_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("eResult", &AssociateWithClanResult_t::m_eResult)
 	;
 	py::class_<ComputeNewPlayerCompatibilityResult_t> _computenewplayercompatibilityresult_t(m, "ComputeNewPlayerCompatibilityResult");
 	_computenewplayercompatibilityresult_t
 		.def("_addr", [](ComputeNewPlayerCompatibilityResult_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("eResult", &ComputeNewPlayerCompatibilityResult_t::m_eResult)
 		.def_readwrite("cPlayersThatDontLikeCandidate", &ComputeNewPlayerCompatibilityResult_t::m_cPlayersThatDontLikeCandidate)
 		.def_readwrite("cPlayersThatCandidateDoesntLike", &ComputeNewPlayerCompatibilityResult_t::m_cPlayersThatCandidateDoesntLike)
 		.def_readwrite("cClanPlayersThatDontLikeCandidate", &ComputeNewPlayerCompatibilityResult_t::m_cClanPlayersThatDontLikeCandidate)
 		.def_readwrite("SteamIDCandidate", &ComputeNewPlayerCompatibilityResult_t::m_SteamIDCandidate)
 	;
 	py::class_<GSStatsReceived_t> _gsstatsreceived_t(m, "GSStatsReceived");
 	_gsstatsreceived_t
 		.def("_addr", [](GSStatsReceived_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("eResult", &GSStatsReceived_t::m_eResult)
 		.def_readwrite("steamIDUser", &GSStatsReceived_t::m_steamIDUser)
 	;
 	py::class_<GSStatsStored_t> _gsstatsstored_t(m, "GSStatsStored");
 	_gsstatsstored_t
 		.def("_addr", [](GSStatsStored_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("eResult", &GSStatsStored_t::m_eResult)
 		.def_readwrite("steamIDUser", &GSStatsStored_t::m_steamIDUser)
 	;
 	py::class_<GSStatsUnloaded_t> _gsstatsunloaded_t(m, "GSStatsUnloaded");
 	_gsstatsunloaded_t
 		.def("_addr", [](GSStatsUnloaded_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("steamIDUser", &GSStatsUnloaded_t::m_steamIDUser)
 	;
 	py::class_<SteamNetworkingFakeIPResult_t> _steamnetworkingfakeipresult_t(m, "SteamNetworkingFakeIPResult");
 	_steamnetworkingfakeipresult_t
 		.def("_addr", [](SteamNetworkingFakeIPResult_t& self) { return reinterpret_cast<uintptr_t>(&self); })
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
 		.def("_addr", [](SteamIPAddress_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def("IsSet", &SteamAPI_SteamIPAddress_t_IsSet)
 		.def_property_readonly("rgubIPv6",
			[](SteamIPAddress_t& x) { return FixedArrayView<uint8, 16>{ x.m_rgubIPv6 }; },
			py::return_value_policy::reference_internal)
 		.def_readwrite("eType", &SteamIPAddress_t::m_eType)
 	;
 	py::class_<FriendGameInfo_t> _friendgameinfo_t(m, "FriendGameInfo");
 	_friendgameinfo_t
 		.def("_addr", [](FriendGameInfo_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("gameID", &FriendGameInfo_t::m_gameID)
 		.def_readwrite("unGameIP", &FriendGameInfo_t::m_unGameIP)
 		.def_readwrite("usGamePort", &FriendGameInfo_t::m_usGamePort)
 		.def_readwrite("usQueryPort", &FriendGameInfo_t::m_usQueryPort)
 		.def_readwrite("steamIDLobby", &FriendGameInfo_t::m_steamIDLobby)
 	;
 	py::class_<MatchMakingKeyValuePair_t> _matchmakingkeyvaluepair_t(m, "MatchMakingKeyValuePair");
 	_matchmakingkeyvaluepair_t
 		.def("_addr", [](MatchMakingKeyValuePair_t& self) { return reinterpret_cast<uintptr_t>(&self); })
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
 		.def("_addr", [](servernetadr_t& self) { return reinterpret_cast<uintptr_t>(&self); })
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
 		.def("_addr", [](gameserveritem_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def("GetName", &SteamAPI_gameserveritem_t_GetName)
 		.def("SetName", [](gameserveritem_t* self, uintptr_t pName) { 
			auto* ptr0 = reinterpret_cast<const char *>(pName);
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
 		.def("_addr", [](SteamPartyBeaconLocation_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("eType", &SteamPartyBeaconLocation_t::m_eType)
 		.def_readwrite("ulLocationID", &SteamPartyBeaconLocation_t::m_ulLocationID)
 	;
 	py::class_<SteamParamStringArray_t> _steamparamstringarray_t(m, "SteamParamStringArray");
 	_steamparamstringarray_t
 		.def("_addr", [](SteamParamStringArray_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("nNumStrings", &SteamParamStringArray_t::m_nNumStrings)
 	;
 	py::class_<LeaderboardEntry_t> _leaderboardentry_t(m, "LeaderboardEntry");
 	_leaderboardentry_t
 		.def("_addr", [](LeaderboardEntry_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("steamIDUser", &LeaderboardEntry_t::m_steamIDUser)
 		.def_readwrite("nGlobalRank", &LeaderboardEntry_t::m_nGlobalRank)
 		.def_readwrite("nScore", &LeaderboardEntry_t::m_nScore)
 		.def_readwrite("cDetails", &LeaderboardEntry_t::m_cDetails)
 		.def_readwrite("hUGC", &LeaderboardEntry_t::m_hUGC)
 	;
 	py::class_<P2PSessionState_t> _p2psessionstate_t(m, "P2PSessionState");
 	_p2psessionstate_t
 		.def("_addr", [](P2PSessionState_t& self) { return reinterpret_cast<uintptr_t>(&self); })
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
 		.def("_addr", [](InputAnalogActionData_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("eMode", &InputAnalogActionData_t::eMode)
 		.def_readwrite("x", &InputAnalogActionData_t::x)
 		.def_readwrite("y", &InputAnalogActionData_t::y)
 		.def_readwrite("bActive", &InputAnalogActionData_t::bActive)
 	;
 	py::class_<InputDigitalActionData_t> _inputdigitalactiondata_t(m, "InputDigitalActionData");
 	_inputdigitalactiondata_t
 		.def("_addr", [](InputDigitalActionData_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("bState", &InputDigitalActionData_t::bState)
 		.def_readwrite("bActive", &InputDigitalActionData_t::bActive)
 	;
 	py::class_<InputMotionData_t> _inputmotiondata_t(m, "InputMotionData");
 	_inputmotiondata_t
 		.def("_addr", [](InputMotionData_t& self) { return reinterpret_cast<uintptr_t>(&self); })
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
 		.def("_addr", [](SteamInputActionEvent_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("controllerHandle", &SteamInputActionEvent_t::controllerHandle)
 		.def_readwrite("eEventType", &SteamInputActionEvent_t::eEventType)
 		.def_readwrite("analogAction", &SteamInputActionEvent_t::analogAction)
 	;
 	py::class_<SteamUGCDetails_t> _steamugcdetails_t(m, "SteamUGCDetails");
 	_steamugcdetails_t
 		.def("_addr", [](SteamUGCDetails_t& self) { return reinterpret_cast<uintptr_t>(&self); })
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
 		.def("_addr", [](SteamItemDetails_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("itemId", &SteamItemDetails_t::m_itemId)
 		.def_readwrite("iDefinition", &SteamItemDetails_t::m_iDefinition)
 		.def_readwrite("unQuantity", &SteamItemDetails_t::m_unQuantity)
 		.def_readwrite("unFlags", &SteamItemDetails_t::m_unFlags)
 	;
 	py::class_<RemotePlayInputMouseMotion_t> _remoteplayinputmousemotion_t(m, "RemotePlayInputMouseMotion");
 	_remoteplayinputmousemotion_t
 		.def("_addr", [](RemotePlayInputMouseMotion_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("bAbsolute", &RemotePlayInputMouseMotion_t::m_bAbsolute)
 		.def_readwrite("flNormalizedX", &RemotePlayInputMouseMotion_t::m_flNormalizedX)
 		.def_readwrite("flNormalizedY", &RemotePlayInputMouseMotion_t::m_flNormalizedY)
 		.def_readwrite("nDeltaX", &RemotePlayInputMouseMotion_t::m_nDeltaX)
 		.def_readwrite("nDeltaY", &RemotePlayInputMouseMotion_t::m_nDeltaY)
 	;
 	py::class_<RemotePlayInputMouseWheel_t> _remoteplayinputmousewheel_t(m, "RemotePlayInputMouseWheel");
 	_remoteplayinputmousewheel_t
 		.def("_addr", [](RemotePlayInputMouseWheel_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("eDirection", &RemotePlayInputMouseWheel_t::m_eDirection)
 		.def_readwrite("flAmount", &RemotePlayInputMouseWheel_t::m_flAmount)
 	;
 	py::class_<RemotePlayInputKey_t> _remoteplayinputkey_t(m, "RemotePlayInputKey");
 	_remoteplayinputkey_t
 		.def("_addr", [](RemotePlayInputKey_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("eScancode", &RemotePlayInputKey_t::m_eScancode)
 		.def_readwrite("unModifiers", &RemotePlayInputKey_t::m_unModifiers)
 		.def_readwrite("unKeycode", &RemotePlayInputKey_t::m_unKeycode)
 	;
 	py::class_<RemotePlayInput_t> _remoteplayinput_t(m, "RemotePlayInput");
 	_remoteplayinput_t
 		.def("_addr", [](RemotePlayInput_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_readwrite("unSessionID", &RemotePlayInput_t::m_unSessionID)
 		.def_readwrite("eType", &RemotePlayInput_t::m_eType)
 		.def_property("padding",
			[](const RemotePlayInput_t& x) { return get_char_array(x.padding); },
			[](RemotePlayInput_t& x, const std::string& s) { set_char_array(x.padding, s); })
 	;
 	py::class_<SteamNetworkingIPAddr> _steamnetworkingipaddr(m, "SteamNetworkingIPAddr");
 	_steamnetworkingipaddr
 		.def("_addr", [](SteamNetworkingIPAddr& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def("Clear", &SteamAPI_SteamNetworkingIPAddr_Clear)
 		.def("IsIPv6AllZeros", &SteamAPI_SteamNetworkingIPAddr_IsIPv6AllZeros)
 		.def("SetIPv6", [](SteamNetworkingIPAddr* self, uintptr_t ipv6, uint16 nPort) { 
			auto* ptr0 = reinterpret_cast<const uint8 *>(ipv6);
			SteamAPI_SteamNetworkingIPAddr_SetIPv6(self, ptr0, nPort); }, py::arg("ipv6"), py::arg("nPort"))
 		.def("SetIPv4", &SteamAPI_SteamNetworkingIPAddr_SetIPv4, py::arg("nIP"), py::arg("nPort"))
 		.def("IsIPv4", &SteamAPI_SteamNetworkingIPAddr_IsIPv4)
 		.def("GetIPv4", &SteamAPI_SteamNetworkingIPAddr_GetIPv4)
 		.def("SetIPv6LocalHost", &SteamAPI_SteamNetworkingIPAddr_SetIPv6LocalHost, py::arg("nPort"))
 		.def("IsLocalHost", &SteamAPI_SteamNetworkingIPAddr_IsLocalHost)
 		.def("ToString", [](SteamNetworkingIPAddr* self, uintptr_t buf, uint32 cbBuf, bool bWithPort) { 
			auto* ptr0 = reinterpret_cast<char *>(buf);
			SteamAPI_SteamNetworkingIPAddr_ToString(self, ptr0, cbBuf, bWithPort); }, py::arg("buf"), py::arg("cbBuf"), py::arg("bWithPort"))
 		.def("ParseString", [](SteamNetworkingIPAddr* self, uintptr_t pszStr) { 
			auto* ptr0 = reinterpret_cast<const char *>(pszStr);
			SteamAPI_SteamNetworkingIPAddr_ParseString(self, ptr0); }, py::arg("pszStr"))
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
 		.def("_addr", [](SteamNetworkingIdentity& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def("Clear", &SteamAPI_SteamNetworkingIdentity_Clear)
 		.def("IsInvalid", &SteamAPI_SteamNetworkingIdentity_IsInvalid)
 		.def("SetSteamID", &SteamAPI_SteamNetworkingIdentity_SetSteamID, py::arg("steamID"))
 		.def("GetSteamID", &SteamAPI_SteamNetworkingIdentity_GetSteamID)
 		.def("SetSteamID64", &SteamAPI_SteamNetworkingIdentity_SetSteamID64, py::arg("steamID"))
 		.def("GetSteamID64", &SteamAPI_SteamNetworkingIdentity_GetSteamID64)
 		.def("SetXboxPairwiseID", [](SteamNetworkingIdentity* self, uintptr_t pszString) { 
			auto* ptr0 = reinterpret_cast<const char *>(pszString);
			SteamAPI_SteamNetworkingIdentity_SetXboxPairwiseID(self, ptr0); }, py::arg("pszString"))
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
			auto* ptr0 = reinterpret_cast<const char *>(pszString);
			SteamAPI_SteamNetworkingIdentity_SetGenericString(self, ptr0); }, py::arg("pszString"))
 		.def("GetGenericString", &SteamAPI_SteamNetworkingIdentity_GetGenericString)
 		.def("SetGenericBytes", [](SteamNetworkingIdentity* self, uintptr_t data, uint32 cbLen) { 
			auto* ptr0 = reinterpret_cast<const void *>(data);
			SteamAPI_SteamNetworkingIdentity_SetGenericBytes(self, ptr0, cbLen); }, py::arg("data"), py::arg("cbLen"))
 		.def("GetGenericBytes", &SteamAPI_SteamNetworkingIdentity_GetGenericBytes, py::arg("cbLen"))
 		.def("__eq__", &SteamAPI_SteamNetworkingIdentity_IsEqualTo, py::arg("x"))
 		.def("ToString", [](SteamNetworkingIdentity* self, uintptr_t buf, uint32 cbBuf) { 
			auto* ptr0 = reinterpret_cast<char *>(buf);
			SteamAPI_SteamNetworkingIdentity_ToString(self, ptr0, cbBuf); }, py::arg("buf"), py::arg("cbBuf"))
 		.def("ParseString", [](SteamNetworkingIdentity* self, uintptr_t pszStr) { 
			auto* ptr0 = reinterpret_cast<const char *>(pszStr);
			SteamAPI_SteamNetworkingIdentity_ParseString(self, ptr0); }, py::arg("pszStr"))
 		.def_readwrite("eType", &SteamNetworkingIdentity::m_eType)
 		.def_readwrite("cbSize", &SteamNetworkingIdentity::m_cbSize)
 		.def_property("szUnknownRawString",
			[](const SteamNetworkingIdentity& x) { return get_char_array(x.m_szUnknownRawString); },
			[](SteamNetworkingIdentity& x, const std::string& s) { set_char_array(x.m_szUnknownRawString, s); })
 	;
 	py::class_<SteamNetConnectionInfo_t> _steamnetconnectioninfo_t(m, "SteamNetConnectionInfo");
 	_steamnetconnectioninfo_t
 		.def("_addr", [](SteamNetConnectionInfo_t& self) { return reinterpret_cast<uintptr_t>(&self); })
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
 		.def("_addr", [](SteamNetConnectionRealTimeStatus_t& self) { return reinterpret_cast<uintptr_t>(&self); })
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
 		.def("_addr", [](SteamNetConnectionRealTimeLaneStatus_t& self) { return reinterpret_cast<uintptr_t>(&self); })
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
 		.def("_addr", [](SteamNetworkingMessage_t& self) { return reinterpret_cast<uintptr_t>(&self); })
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
 		.def("_addr", [](SteamNetworkPingLocation_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def_property_readonly("data",
			[](SteamNetworkPingLocation_t& x) { return FixedArrayView<uint8, 512>{ x.m_data }; },
			py::return_value_policy::reference_internal)
 	;
 	py::class_<SteamNetworkingConfigValue_t> _steamnetworkingconfigvalue_t(m, "SteamNetworkingConfigValue");
 	_steamnetworkingconfigvalue_t
 		.def("_addr", [](SteamNetworkingConfigValue_t& self) { return reinterpret_cast<uintptr_t>(&self); })
 		.def(py::init<>())
 		.def("SetInt32", &SteamAPI_SteamNetworkingConfigValue_t_SetInt32, py::arg("eVal"), py::arg("data"))
 		.def("SetInt64", &SteamAPI_SteamNetworkingConfigValue_t_SetInt64, py::arg("eVal"), py::arg("data"))
 		.def("SetFloat", &SteamAPI_SteamNetworkingConfigValue_t_SetFloat, py::arg("eVal"), py::arg("data"))
 		.def("SetPtr", [](SteamNetworkingConfigValue_t* self, ESteamNetworkingConfigValue eVal, uintptr_t data) { 
			auto* ptr1 = reinterpret_cast<void *>(data);
			SteamAPI_SteamNetworkingConfigValue_t_SetPtr(self, eVal, ptr1); }, py::arg("eVal"), py::arg("data"))
 		.def("SetString", [](SteamNetworkingConfigValue_t* self, ESteamNetworkingConfigValue eVal, uintptr_t data) { 
			auto* ptr1 = reinterpret_cast<const char *>(data);
			SteamAPI_SteamNetworkingConfigValue_t_SetString(self, eVal, ptr1); }, py::arg("eVal"), py::arg("data"))
 		.def_readwrite("eValue", &SteamNetworkingConfigValue_t::m_eValue)
 		.def_readwrite("eDataType", &SteamNetworkingConfigValue_t::m_eDataType)
 		.def_readwrite("val", &SteamNetworkingConfigValue_t::m_val)
 	;
 	py::class_<SteamDatagramHostedAddress> _steamdatagramhostedaddress(m, "SteamDatagramHostedAddress");
 	_steamdatagramhostedaddress
 		.def("_addr", [](SteamDatagramHostedAddress& self) { return reinterpret_cast<uintptr_t>(&self); })
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
 		.def("_addr", [](SteamDatagramGameCoordinatorServerLogin& self) { return reinterpret_cast<uintptr_t>(&self); })
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
 	
}
