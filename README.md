Fennec Overlay
==============

The Fennec Overlay is a rootless and purely distributed Internet Protocol and Ethernet replacement.

It seeks to make attempts to compromise anonymity and authenticity of users mathematically
improbable, while only incurring a minimal performance penalty even when fully anonymous.

It combines many existing but isolated and obscure projects and intitiatives, 
and integrates them into a cohesive whole. 

Fennec will eventually eventually be completely rewritten, with every dependency
of every component being forked and independantly maintained for perfect integration and cohesion.

In the meantime, the top-most layers of Fennec can operate over existing low-level
network protocols.


Concept
-------

Fennec aims to virtualize and converge all communications, connecting your device's system bus 
directly to that of those you care about.

In other words, Fennec is similar to a combination of Juniper QFabric, Cray Gemini Interconnect, 
and even more experimental and innovative forms of crossbar optical fabric switching 
generally used in multiprocessor interconnects and mutlichassis switching fabrics.

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

The list of components and a description of each layer can be found in the "Technical Stuff" section.



Yo Dawg
=======

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

The reference implementation of the proof of concept is nothing like the true Fennec Overlay, but
should be close enough to the high-level design to prove the concept.


Further Notes
-------------

This Project is in its infancy, and only exists on GitHub at all because my friends have been 
pestering me incessantly about creating a repo for it.

I hope that this document helps to describe the concept for Fennec and the ambitious scope and visions that 
it represents, and how desperately the world actually needs the Fennec Overlay.
