import build.steamworks as steam

steam.init()

kv = steam.MatchMakingKeyValuePair()
kv.szKey = 'key'
kv.szValue = 'value'
assert(kv.szKey == 'key')
assert(kv.szValue == 'value')

fi = steam.FriendGameInfo()
fi.steamIDLobby = steam.SteamID(42)
assert(int(fi.steamIDLobby) == 42)

ffl = steam.FriendsEnumerateFollowingList()
ffl.rgSteamID[1] = steam.SteamID(43)
print(ffl.rgSteamID)
assert(int(ffl.rgSteamID[1]) == 43)
assert(int(ffl.rgSteamID.to_list()[1]) == 43)

ip1 = steam.SteamNetworkingIPAddr()
ip1.port = 1221
ip2 = steam.SteamNetworkingIPAddr()
ip2.port = 1221
assert(ip1 == ip2)
ip2.port = 45
assert(ip1 != ip2)

steam.shutdown()
