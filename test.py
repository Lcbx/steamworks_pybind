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
ffl.rgSteamID[-1] = steam.SteamID(20)

assert(int(ffl.rgSteamID[1]) == 43)
assert(int(ffl.rgSteamID[49]) == 20)
assert(int(ffl.rgSteamID.to_list()[1]) == 43)

# assert that out of bounds throw
passed = True
try:
    ffl.rgSteamID[50] = steam.SteamID(255)
    passed = False
except:
    pass
try:
    ffl.rgSteamID[-51] = steam.SteamID(255)
    passed = False
except:
    pass
assert(passed)

ip1 = steam.SteamNetworkingIPAddr()
ip1.port = 1221
ip2 = steam.SteamNetworkingIPAddr()
ip2.port = 1221
assert(ip1 == ip2)
ip2.port = 45
assert(ip1 != ip2)

import ctypes
buf = ctypes.create_string_buffer(256)
addr = ctypes.addressof(buf)
buf[0] = 69

ip2.ToString(buf = addr, cbBuf=256, bWithPort = True)
assert( '45' in bytes(buf).decode() )

steam.shutdown()
