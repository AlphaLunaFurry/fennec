Fennec Overlay
--------------

The Fennec Overlay is a rootless and purely distributed Internet Protocol and Ethernet replacement.

It seeks to embed and integrate cryptographic primitives, authentication systems, advanced onion routing,
active darknet web-of-trust authenticity verification and certificate revocation into the lowest-level
protocols feasible, while only incurring a minimal performance penalty even when cranked up to 11.

It combines many existing but isolated and obscure projects and intitiatives, 
and integrates them into a cohesive whole. 

Fennec will eventually be completely rewritten, with every dependency
of every component being forked, independantly maintained, and slowly redesigned, rewritten, reimplemented,
with 100% original and native codebases (released under the WTFPL) for perfect integration and cohesion.

In the meantime, the top-most layers of Fennec can operate over existing low-level
network protocols.


WTFPL
-----

In case you were curious, the WTFPL is an uncopyright license, the Do What The Fuck You Want Public License.

The full legal code can be found at the repo's root, LICENSE.txt. :3

The TL;DR version is: rather than choosing between hard copyleft or strict propietary licensing, it
approximates public domain as closely as is possible when surrounded by mind-numbing copyright racketeering.



Concept
=======

Fennec aims to virtualize and converge all communications, connecting your device's system bus 
directly to that of those you care about.

By taking the most bold, visionary, and aggressive features and improvements of each model and concept, 
Fennec becomes a very ambitous, audacious and dominant new project that seeks to pin the Web down
and take it without mercy.


Killer Apps
-----------

All significant applications and services that make use of the Fennec protocol stack
are very carefully integrated and designed to scale globally, hundreds of millions
of concurrent users at any given time in each context.

All of these applications DO NOT USE SERVERS AT ALL. Any server-based protocol should
be exposed ONLY OVER LOCALHOST AS AN EMULATION OR GATEWAY.

A very brief list of the primary purposes include:

* SecondLife/OpenSIM grid and sim servers,
* WoW, EVE Online, RIFT, Planetside 2, etc server emulations, lag-less and player-unlimited,
* Wiretap-proof large-scale video chat, over 100,000 in each chat with concurrent HD video streams,
* Familiar Forum, Email, Mailing List, IRC, Jabber/XMPP and other services, fully supporting native clients,
* Blogging, Microblogging (including Tent, Red and Pump protocols), RSS/ATOM feeds, etc,
* BitTorrent and Gnutella protocol integration and tunneling
* Freenet, Retroshare, Tor, I2P, Gnunet, CJDNS and other darknet/anon interoperability and integration,
* Instant, trivial and automagical VPN tunneling, including both IPv4 and IPv6 zeroconf networking,
* MMO-RPG, MMO-RTS, MMO-FPS and other large-scale game engines, fully documented and specifified,
* Extreme paranoid-level security, pseudonymous location-separation, 100% MP-OTR plausable deniability,
* Rigorously documented and specified protcol stack and daemon API
* 100% free and open source (all original code is freely available, released under the WTFPL),

There is a ton more functionality that SHOULD be stable and featureful well before official launch and release.

Before the project is complete, source code WILL NOT be published here, only available on an invite-only basis.

The reason for this is to have a fully-functional solution with viable killer applications upon launch.


Although it sounds like the massively multiplayer shared game worlds or secondlife-like virtual worlds
would be genocide-inducingly lag-infested, the sad truth is that a P2P overlay is almost certainly
going to be shockingly lag-free, with zero character rubberbanding, and instantanious toon rezzing.

That is pretty sad and pathetic.

Proof-of-Concept
----------------

In the interim for proof-of-concept and initial experimentation, we will use pre-existing software
to emulate the abstract model.

These pre-existing tools are obviously unsuitable in the long run, but permit a very quick-and-dirty
implementation for demonstrative and educational purposes.

The list of components and a description of each layer can be found in the "doc/technical.md" file.


Yo Dawg
=======

Obviously, the Fennec Overlay, by necessity, will need to have a very sophisticated overlay
design and architecture to tunnel over the existing Internet Protocol infrastructure and protocols.

By this, I mean UDP with elaborate hole-punching.

Basically, it's a technique I developed personally based on the PWNAT and P2P-STUN methods.

Eventually, however, it will be the other way around, with Fennec carrying 
legacy IPv4 and IPv6 packets over it similarly to what carrier Ethernet and MPLS is used for today.

In other words, the primary functionality of extremely-scalable VPN networking will become
the dominant IP internetwork, rather than the broken, laughingly-insecure, gum-and-paperclip
Internet we know and love. This crap has got to go.


This Project
------------

For the reference implementation and proof of concept, we will use Python.

The overlay networking will use TeleHash, ZeroMQ, MsgPack, libSwift, Twisted, SQLAlchemy and ProtoBuf.

The cryptography will largely be handled by NaCl, GNU Privacy Guard, Off-The-Record and OpenSSL.

The demonstration application will use SCons, wxWidgets, and for Windows ports, Py2Exe, 
NSIS and the BattleTorrent plug-in for NSIS.

The reference implementation of the proof of concept is nothing like the true Fennec Overlay, but
should be a close enough approximation of the high-level design to prove the concept.


Further Notes
=============

This Project is in its infancy, and only exists on GitHub at all because my friends have been 
pestering me incessantly about creating a repository for it.

I hope that the documentation, whitepapers, dissertations, treatises, manuals and specifications
help to describe the concept for Fennec and the ambitious scope and vision that it represents, 
and how desperately the world actually needs the Fennec Overlay.
