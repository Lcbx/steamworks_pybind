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

steam.shutdown()
