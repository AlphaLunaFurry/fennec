Technical Stuff
===============


Technical Introduction
----------------------

What follows you can safely skip past to "Replacing all 7 OSI Layers" if you aren't
a super-genius mad scientist who builds robot armies and death rays in your backyard 
laboratory of evil and doomy dooms of doom.

Fennec is similar to an eclectic hybridization of Juniper QFabric, Cray Gemini Interconnect, 
and crossbar optical fabric switching generally used in multiprocessor interconnects
and mutlichassis switching fabrics.



Proof-of-Concept
================

In the interim for proof-of-concept and initial experimentation, we will use pre-existing software
to emulate the abstract model.

These pre-existing tools are obviously unsuitable in the long run, but permit a very quick-and-dirty
implementation for demonstrative and educational purposes.

The basic interim model is described below.


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
