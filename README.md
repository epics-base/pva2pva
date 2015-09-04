PV Access to PV Access protocol gateway (aka. proxy)


Theory of Operation

The GW maintains a Channel Cache, which is a dictionary of client side channels
(shared_ptr<epics::pvAccess::Channel> instances)
in the NEVER_CONNECTED or CONNECTED states.

Each entry also has an activity flag and reference count.

The activity flag is set each time the server side receives a search request for a PV.

The reference count is incremented for each active server side channel.

Periodically the cache is iterated and any client channels with !activity and count==0 are dropped.
In addition the activity flag is unconditionally cleared.


Name search handling

The server side listens for name search requests.
When a request is received the channel cache is searched.
If no entry exists, then one is created and no further action is taken.
If an entry exists, but the client channel is not connected, then it's activiy flag is set and no further action is taken.
If a connected entry exists, then an affirmative response is sent to the requester.


When a channel create request is received, the channel cache is checked.
If no connected entry exists, then the request is failed.


Structure associations

ServerChannelProvider 1->1 ChannelCache  (composed)

ChannelCache 1->N ChannelCacheEntry  (map<shared_ptr<E> >)
ChannelCache :: cacheLock

ChannelCacheEntry 1->1 ChannelCache (C*)
ChannelCacheEntry 1->1 Channel (PVA Client) (shared_ptr<C>)

Channel (PVA Client) 1->1 CRequester (shared_ptr<R>)
Channel :: lock

CRequester 1->1 ChannelCacheEntry (weak_ptr<E>)

ChannelCacheEntry 1->N GWChannel  (std<C*>)

GWChannel 1->1 ChannelCacheEntry  (shared_ptr<E>)



ServerChannelRequesterImpl::channelStateChange() - placeholder, needs implementation
