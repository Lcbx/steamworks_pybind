import ctypes
import time
import pytest
from uuid import uuid4

import build.steamworks as steam

# run the tests from just running this script
if __name__ == "__main__":
	from argparse import ArgumentParser
	from sys import exit

	parser = ArgumentParser()
	parser.add_argument('test', nargs='?', help='launch a single test if specified by name')
	args = parser.parse_args()

	arg = __file__
	if (test_name := args.test) and test_name.startswith('test'):
		arg += f'::{test_name}'

	code = pytest.main(["-qs", arg])
	exit(code)


@pytest.fixture(scope="session", autouse=True)
def steam_api():
	assert steam.init()
	yield
	steam.shutdown()


def pump_callbacks(seconds=0.25):
	sockets = steam.SteamNetworkingSockets()
	end = time.time() + seconds
	while time.time() < end:
		sockets.RunCallbacks()
		time.sleep(0.01)


def wait_until(predicate, timeout=10.0):
	sockets = steam.SteamNetworkingSockets()
	end = time.time() + timeout
	while time.time() < end:
		sockets.RunCallbacks()
		if value := predicate():
			return value
		time.sleep(0.02)

	raise TimeoutError("timed out")


def wait_call_result(call, result, timeout=15.0):
	utils = steam.SteamUtils()
	sockets = steam.SteamNetworkingSockets()
	failed = ctypes.c_bool(False)
	result_type = type(result)

	end = time.time() + timeout
	while time.time() < end:
		sockets.RunCallbacks()

		ok = utils.GetAPICallResult(
			hSteamAPICall=call,
			pCallback=result.ptr,
			cubCallback=result_type.nbytes,
			iCallbackExpected=result_type.callback_id,
			pbFailed=ctypes.addressof(failed),
		)

		if ok:
			assert failed.value is False
			return result

		time.sleep(0.01)

	raise TimeoutError("Steam API call timed out")


def test_simple_struct_string_fields():
	kv = steam.MatchMakingKeyValuePair()

	kv.szKey = "key"
	kv.szValue = "value"

	assert kv.szKey == "key"
	assert kv.szValue == "value"


def test_nested_steam_id_field():
	fi = steam.FriendGameInfo()

	fi.steamIDLobby = steam.SteamID(42)

	assert int(fi.steamIDLobby) == 42


def test_fixed_array_indexing_and_to_list():
	ffl = steam.FriendsEnumerateFollowingList()

	ffl.rgSteamID[1] = steam.SteamID(43)
	ffl.rgSteamID[-1] = steam.SteamID(20)

	assert int(ffl.rgSteamID[1]) == 43
	assert int(ffl.rgSteamID[49]) == 20
	assert int(ffl.rgSteamID.to_list()[1]) == 43
	assert int(ffl.rgSteamID.to_list()[49]) == 20


def test_fixed_array_bounds_checks():
	ffl = steam.FriendsEnumerateFollowingList()

	with pytest.raises(Exception):
		ffl.rgSteamID[50] = steam.SteamID(255)

	with pytest.raises(Exception):
		ffl.rgSteamID[-51] = steam.SteamID(255)

	with pytest.raises(Exception):
		_ = ffl.rgSteamID[50]

	with pytest.raises(Exception):
		_ = ffl.rgSteamID[-51]


def test_value_type_equality():
	ip1 = steam.SteamNetworkingIPAddr()
	ip2 = steam.SteamNetworkingIPAddr()

	ip1.port = 1221
	ip2.port = 1221

	assert ip1 == ip2

	ip2.port = 45

	assert ip1 != ip2


def test_out_buffer_to_string():
	ip = steam.SteamNetworkingIPAddr()
	ip.port = 45

	buf = ctypes.create_string_buffer(256)
	addr = ctypes.addressof(buf)

	ip.ToString(buf=addr, cbBuf=256, bWithPort=True)

	text = bytes(buf).decode(errors="ignore")

	assert "45" in text


def test_steam_id_int_conversion_and_equality():
	a = steam.SteamID(123)
	b = steam.SteamID(123)
	c = steam.SteamID(456)

	assert int(a) == 123
	assert a == b
	assert a != c

def test_enum_values():
	assert steam.ESteamNetConnectionEnd.Invalid == 0
	assert steam.ESteamNetConnectionEnd.App_Min == 1000
	assert steam.ESteamNetConnectionEnd.Local_ManyRelayConnectivity == 3002

def test_user_identity():
	user = steam.SteamUser()

	steam_id = user.GetSteamID()

	assert steam_id is not None
	assert int(steam_id) > 0


def test_friends_persona_name():
	friends = steam.SteamFriends()

	name = friends.GetPersonaName()

	assert isinstance(name, str)
	assert name != ""


def test_friends_list_is_readable():
	friends = steam.SteamFriends()
	flag = steam.EFriendFlags.Immediate
	count = friends.GetFriendCount(flag)

	assert isinstance(count, int)
	assert count >= 0

	for i in range(min(count, 5)):
		friend_id = friends.GetFriendByIndex(i, flag)
		friend_name = friends.GetFriendPersonaName(friend_id)

		assert int(friend_id) > 0
		assert isinstance(friend_name, str)


def test_apps_app_id():
	apps = steam.SteamApps()

	app_id = steam.SteamUtils().GetAppID()

	assert isinstance(app_id, int)
	assert app_id > 0


def test_utils_seconds_since_app_active():
	utils = steam.SteamUtils()

	seconds = utils.GetSecondsSinceAppActive()

	assert isinstance(seconds, int)
	assert seconds >= 0

def create_lobby(flag):
	matchmaking = steam.SteamMatchmaking()

	call = matchmaking.CreateLobby( flag, 4 )

	result = steam.LobbyCreated()
	result = wait_call_result(call, result)
	lobby_id = result.ulSteamIDLobby

	assert(int(lobby_id) > 0)
	assert(result.eResult == steam.EResult.OK)

	return result.ulSteamIDLobby


def create_private_lobby():
	return create_lobby(steam.ELobbyType.Private)


def test_create_private_lobby_and_leave():
	matchmaking = steam.SteamMatchmaking()
	lobby_id = create_private_lobby()
	try:
		assert int(lobby_id) > 0

		member_count = matchmaking.GetNumLobbyMembers(lobby_id)
		assert member_count >= 1

		owner = matchmaking.GetLobbyOwner(lobby_id)
		assert int(owner) > 0

	finally:
		matchmaking.LeaveLobby(lobby_id)
		pump_callbacks()


def test_lobby_data_roundtrip():
	matchmaking = steam.SteamMatchmaking()
	lobby_id = create_private_lobby()
	try:
		key = "pytest_key"
		value = "value-{uuid4()}"

		ok = matchmaking.SetLobbyData(lobby_id, key, value)
		assert ok is True

		observed = wait_until(
			lambda: matchmaking.GetLobbyData(lobby_id, key) == value,
			timeout=5.0,
		)

		assert observed is True

	finally:
		matchmaking.LeaveLobby(lobby_id)
		pump_callbacks()


def test_lobby_member_data_roundtrip():
	matchmaking = steam.SteamMatchmaking()
	lobby_id = create_private_lobby()
	try:

		own_id = steam.SteamUser().GetSteamID()

		key = "pytest_member_key"
		value = f"member-{uuid4()}"

		matchmaking.SetLobbyMemberData(lobby_id, key, value)

		observed = wait_until(
			lambda: matchmaking.GetLobbyMemberData(lobby_id, own_id, key) == value,
			timeout=5.0,
		)

		assert observed is True

	finally:
		matchmaking.LeaveLobby(lobby_id)
		pump_callbacks()


def test_lobby_members_include_self():
	matchmaking = steam.SteamMatchmaking()
	lobby_id = create_private_lobby()
	try:
		own_id = int(steam.SteamUser().GetSteamID())
		count = matchmaking.GetNumLobbyMembers(lobby_id)

		members = [
			int(matchmaking.GetLobbyMemberByIndex(lobby_id, i))
			for i in range(count)
		]

		assert own_id in members

	finally:
		matchmaking.LeaveLobby(lobby_id)
		pump_callbacks()


def test_lobby_joinable_toggle():
	matchmaking = steam.SteamMatchmaking()
	lobby_id = create_private_lobby()
	try:
		assert matchmaking.SetLobbyJoinable(lobby_id, False) is True
		assert matchmaking.SetLobbyJoinable(lobby_id, True) is True

	finally:
		matchmaking.LeaveLobby(lobby_id)
		pump_callbacks()


def test_lobby_type_change():
	matchmaking = steam.SteamMatchmaking()
	lobby_id = create_private_lobby()
	try:
		assert matchmaking.SetLobbyType(lobby_id, steam.ELobbyType.FriendsOnly) is True
		assert matchmaking.SetLobbyType(lobby_id, steam.ELobbyType.Private) is True

	finally:
		matchmaking.LeaveLobby(lobby_id)
		pump_callbacks()


def test_find_public_lobby_by_metadata():
	matchmaking = steam.SteamMatchmaking()
	lobby_id = create_lobby(steam.ELobbyType.Invisible)
	try:
		#assert matchmaking.SetLobbyJoinable(lobby_id, True) is True

		key = "pytest_search_key"
		value = f"search-{uuid4()}"

		assert matchmaking.SetLobbyData(lobby_id, key, value) is True

		# it seems steam takes a lil time to publish a lobby
		pump_callbacks(0.5)

		#matchmaking.AddRequestLobbyListResultCountFilter(10)
		#matchmaking.AddRequestLobbyListDistanceFilter(
		#	steam.ELobbyDistanceFilter.Worldwide
		#)

		matchmaking.AddRequestLobbyListStringFilter(
			key,
			value,
			steam.ELobbyComparison.Equal,
		)

		result = steam.LobbyMatchList()
		call = matchmaking.RequestLobbyList()
		result = wait_call_result(call, result)

		count = result.nLobbiesMatching
		assert count >= 1

		found = False
		for i in range(count):
			candidate = matchmaking.GetLobbyByIndex(i)
			if int(candidate) == int(lobby_id):
				found = True
				break

		assert found

	finally:
		matchmaking.LeaveLobby(lobby_id)
		pump_callbacks()

def test_get_local_user_friend_list_and_names():
	friends = steam.SteamFriends()
	friend_flags = steam.EFriendFlags.Immediate

	friend_count = friends.GetFriendCount(friend_flags)

	assert friend_count >= 0, "GetFriendCount returned -1; is Steam logged in?"

	if friend_count == 0:
		pytest.skip("Local Steam user has no regular friends visible to this app/session")

	friend_entries = []

	print(f"Found {friend_count} Steam friends:")
	for i in range(friend_count):
		friend_id = friends.GetFriendByIndex(i, friend_flags)
		name = friends.GetFriendPersonaName(friend_id)
		print(f"- {name}: {friend_id}")
		assert friend_id is not None
		assert isinstance(name, str)
		assert name != ""