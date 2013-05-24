Replacing all 7 OSI Layers
==========================

The Fennec Overlay's ultimate and final goal is to redesign and rebuild the entire Internet's
protocol stack from the physical network interface design all of the way up to 
application layer protocols, in other words, it is not merely an overlay.

It is an entire protocol stack.

This means that it's throwing out nearly all of the old conventions, tiresome museum artifacts, and 
barely-tolerated failures of the Web and the Internet in general, especially the domain anme system, 
arbitrary addressing, artificial distinctions between devices within the same administrative domain, 
artificial barriers to pervasive encryption, artificial distinctions between frame switching and overlay
routing, and other annoyances.


Cable Layer
-----------

The first layer will consist mostly of multimode fiber. 

Multimode fiber from the plug computer or access
point at the subscriber-side network edge, which aggregates flits within a household or campus to 
the serice provider's node (service distribution platform within outside plant). 

From there, singlemode fiber extends through the containerized datacenters, central offices, internet exchange 
datacenters, and finally across peering and transit cross-connects.


Flit Layer
----------

At the flit switching layer, Juniper/Cray-like flit switching fabric forwards relatively small frames 
which flow over a fabric that explodes the data plane and control plane of the traditional switch chassis, 
and essentially extends the data plane of extremely high-end carrier-grade backplanes 
directly from end to end across the entire global internetwork. 

Basically, think of the wormhole flit switching on individual high-speed system buses, especially 
the interconnects between multiple processors and shared RAM, and then think of that merged together
as one single contiguous backplane, linking every ARM, Intel, AMD, TI and nVidia processor on the globe
directly together as though on a single motherboard.

If you immediately worry about how ridiculously insecure this sounds, that's because you're a smart person. 
However, since flit switched wormhole circuits (similar to MPLS virtual circuits but extremely low-level)
are encrypted end-to-end at the virtual host level, security is an extremely different set of problems.

The network is converged into one huge host system bus, which means end user devices behave and are treated 
like virtual devices. On a home network, all devices behave like one device with multiple users. This
behavior is reflected in the underlying network design. The entire household acts like one device.

The carrier network that household connects to also acts like one single device. The entire multinational
carrier network spanning the entire globe behaves like a single switch backplane.

This carrier-scale data plane is composed of an elastic partial-mesh of switch nodes, which load balance, 
self-heal flit circuits, and dynamically and rapidly regenerate flit wormhole switching circuits during a 
switch node failure or congestion.


Key Layer
---------

Individual identities then layer logically and virtually on top of this global backplane, MAC and IP 
addresses combined and replaced with ECC public keys, and wormhole switching building circuits 
from one identity to another.

The control plane is also decentralized, spread across master nodes and context nodes which exist at places such as 
the home gateway ONT, the network node, scattered in each containerized datacenter, central office, 
and any modern large-scale datacenter. 

The master nodes each collaborate to collectively maintain current active forwarding routes 
in all peer nodes. These all operate on a multicast primitive.

The control plane is composed of a few layers to decentralize the functions of multicast routing context
state synchronization and regeneration.


Context Layer
-------------

The multicast primitive operates similarly to IP Multicast in some ways, but is a lot more elaborate. 
The control channel is distributedly fast-switched, in other words, the route table is distributed, and
multicast context forwarding is cached at all levels.

For better distribution and load balancing, contexts operate on a transport layer. Datagrams intended 
for a specific context (group of identities, each being a group of device contexts, or virtual devices)
are routed at a different layer than individual flits of which datagrams are composed.

These transport-layer forwarding functions are handled by a distinct class of Fennec master nodes, called 
context nodes. The context nodes operate on a higher level than normal switch nodes.

Context nodes take datagrams, parse the keys, maintain smart context routing tables, and relay datagrams
to over other context nodes very smartly, taking geographical location, latency, bandwidth, administrative domains
and proximity or closeness of the majority of the participants all into consideration as factors in forwarding.

In addtion to the control channel handled similarly to IP Multicast, the actual datagrams are smart-multicast, 
where no datagram is transmitted over the same long-haul cable twice, and spread across large geographical
locations quickly, and largely being duplicated as local to the participating devices as possible.

All contexts have a fundemental tendancy toward data permanency. All contexts store recent transmissions in a 
cloudlike way. Application-layer data is serialized, packed into objects and scatter-cached across the network.

This means repeated requests for the same information is returned extremely quickly, all protocols have 
built-in low-level load balancing and acceleration, content distribution and chat history archival.

In addition to this, in high-density clusters of particpants for an ongoing stream, a BitTorrent-like 
transport-layer swarming is used, not unlike libSwift, where Datagrams are exchanged like Bittorrent peices, and 
high-level objects are very rapidly distributed to all interested nodes. 

Nodes who wish to actively encourage object retention should subscribe to the root hashes of those objects, and 
emit heartbeat pings across the master node overlay's DHT. This causes the DHT to constantly regenerate the
context datagram swarm (CDS) state.

Generally, context nodes can be thought of operating on OSI models 3, 4, and 7 in several ways. Specifically, 
context nodes maintain elaborate metrics and link state throughout the global network. It defies the 
tradition of keeping routers fully independent, and behaving more like the control plane of a switching fabric
backplane with a few additional optimizations for reliability and geographical seperation.


Service Layer
-------------

All identities are contexts, and in general, all contexts serve multiple functions. Contexts that serve as
user 'account' or 'username' identification also serve as the 'name' for a groupchat-like context, which permits 
the user in question to distribute and share presence information, status update notifications, publications, 
files, live media streams, biometric and mood metapresence information, geolocation, and other constant feeds.

Rather than require arbitrarilly allocated Autonomous System numbers, the entire network of devices and nodes 
under one administrative domain is identified as a whole by a keypair, which forms the domain context.

Peering with other autonomous systems is thus a seamless and integrated function of the underlying fabric.
This means there is no need for border gateway policies and negotiations as an artificial barrier.

All inter-domain routing and policy negociation is handled by the control nodes, and implemented by 
overlay nodes.

Services, such as virtual worlds, massively multiplayer online roleplaying games, online digital media stores, 
online banking services, digital storefronts, social network interaction, music, movie and ebook sharing, 
presence and status update sharing, instant messaging, group chat rooms, threaded discussions, virtual offices, 
virtual classrooms, live online seminars, screensharing, presentations, remote support control, virtual 
private networking, voice and video telephony, telepresence and telecommuting, realtime collaborative music
recording and remote video editing, massive multiparty voice and video conferencing, live music and video streaming, 
online radio and television, matchmaking and career building, and just about anything else you could want, 
are all implemented over contexts, program objects serialized and distributed in extremely-fast swarms over multicast
contexts which in turn are flooded over flit wormhole circuits.
