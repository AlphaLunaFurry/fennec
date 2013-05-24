Fennec Overlay
==============

The Fennec Overlay is a rootless and purely decentralized Internet Protocol and Ethernet replacement.

It takes the best of peer-to-peer overlay technologies and techniques and puts them directly 
at layer 2, where they belong. 

While it is intended to serve as an overlay initially, the endgame
is to replace IP and carrier Ethernet and tunnel them all over Fennec in the end.

The underlying concept is ultimately extremely simple.


Concept
-------

Fennec aims to virtualize and converge all of communications, connecting your device's system bus 
to that of those you care about.

In other words, Fennec is similar to a combination of Juniper QFabric, Cray Gemini Interconnect, 
and even more experimental and innovative forms of
extremely high-performance crossbar optical fabric switching 
generally used in multiprocessor interconnects
and extremely high-bandwidth mutlichassis switching fabrics.

Generally speaking, the design is essentially an omega clos hypercube network with 
linear network coding and flit wormhole switching.

It resembles the general concept of the Distributed Supercomputer Supernet (SSN) but takes it even further.

Fennec uses a combination of modified chip-level label switching and Kademlia routing 
to achieve perfect decentralization while maintaining realtime speeds very close to wirespeed. 
Fennec is intended to make traffic interception and inspection exceedingly difficult 
for any parties other than those communicating.

Borrowing many concepts from CJDNS, I2P, RetroShare, SecureShare, BitTorrent Chat and IRC DCC SCHAT, the 
Fennec Overlay seeks to solve hard problems with the fundemental failures and critical flaws in the 
current Internet architecture and infrastructure.

While borrowing knowledge and insight from places as diverse as HackBB, Open Cobalt, gnuNET, AnoNet, 
Hyperboria, PSYCed, SwiftIM, OpenSourceSkype, BATMAN, B4RN, Asterisk, FON, Tomato, GNS3 and FreedomBox, 
it seeks to cherrypick the best concepts and ideas of each, and synthesize a new and cohesive whole.

By taking the most bold, visionary, and aggressive features and improvements of each model and concept, 
Fennec becomes a very ambitous, audacious and dominant new project that seeks to pin the Web down
and take it without mercy.

Proof-of-Concept
================

In the interim for proof-of-concept and initial experimentation, we will use pre-existing software
to emulate the abstract model.

These pre-existing tools are obviously unsuitable in the long run, but permit a very quick-and-dirty
implementation for demonstrative and educational purposes.

The basic interim model is as follows:

Primary Anonymity Layer
-----------------------

I2P Invisible Internet Project
http://www.i2p2.de/intro.html

Compatibility Layer 1
---------------------

GarliCat (OnionCat for I2P)
https://www.cypherpunk.at/onioncat/wiki/GarliCat

Primary Overlay
---------------

CJDNS Encrypted Networking Engine
https://github.com/cjdelisle/cjdns/blob/master/rfcs/Whitepaper.md

Secondary Anonymity Layer
-------------------------

Tor (with alternative directory and bridge authorities within cjdns)
https://blog.torproject.org/blog/ipv6-future-i-hear
https://www.torproject.org/docs/tor-manual-dev.html.en

Compatibility Layer 2
---------------------

QuickTun (with nonce nacl crypto; no kill like overkill)
http://wiki.ucis.nl/QuickTun

Secondary Overlay
-----------------

PSYC1 (forked old federated form of PSYC, repurposed to avoid gnuNET)
http://about.psyc.eu/PSYC

FoxCoin (alternative blockchain protected with Tor over CJDNS over I2P)

Bittorrent (Pure DHT over CJDNS, using trackers ONLY for bootstrapping DHT)

App Layer
---------

Any applications would be at this layer, and by convention, should be integrated
with underlying layers if possible.

Relying on DHTs protected from Sybil attacks by relying on the underlying 
friend-to-friend CJDNS to isolate and eliminate Sybil attacks, while relying on
I2P to prevent security compromise and physical attack upon public CJDNS nodes.

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


Yo Dawg I Herd U Liek Networks
==============================

Obviously, the Fennec Overlay, by necessity, will need to have a very sophisticated overlay
design and architecture to tunnel over the existing Internet Protocol infrastructure and protocols.

By this, I mean UDP with elaborate hole-punching.

Eventually, however, it will be the other way around, with Fennec carrying 
legacy IPv4 and IPv6 packets over it similarly to how carrier Ethernet or MPLS is used for today.


This Project
============

For the reference implementation and proof of concept, we will use Python.

The overlay networking will use TeleHash, ZeroMQ, MsgPack, libSwift, Twisted, SQLAlchemy and ProtoBuf.

The cryptography will largely be handled by NaCl, GNU Privacy Guard, Off-The-Record and OpenSSL.

The demonstration application will use SCons, wxWidgets, possibly GLADE, and for Windows ports, Py2Exe, 
NSIS and the BattleTorrent plug-in for NSIS.

The reference implementatio of the proof of concept is nothing like the true Fennec Overlay, but
should be close enough to the high-level design to prove the concept.

Further Notes
-------------

This Project is in its infancy, and only exists on GitHub at all because my friends have been 
pestering me incessantly about creating a repo for it.

I hope that this document helps to describe the concept for Fennec and the ambitious scope and visions that 
it represents, and how desperately the world actually needs the Fennec Overlay.
